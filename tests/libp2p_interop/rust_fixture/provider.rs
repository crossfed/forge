use std::{error::Error, time::Duration};

use futures::StreamExt;
use libp2p::{
    Multiaddr, PeerId, Swarm,
    core::ConnectedPoint,
    kad::{self, store::RecordStore},
    multiaddr::Protocol,
    swarm::{ConnectionId, SwarmEvent, dial_opts::DialOpts},
};
use serde_json::json;

use super::{
    Behaviour, BehaviourEvent, KAD_PROTOCOL, Options, new_swarm, transport_addr, write_json,
};

struct LookupEvidence {
    query_id: kad::QueryId,
    matched_query_id: kad::QueryId,
    stats: kad::QueryStats,
    provider_count: usize,
    connection_id: ConnectionId,
    address: Multiaddr,
    attempts: Vec<LookupAttempt>,
}

struct LookupAttempt {
    query_id: kad::QueryId,
    stats: kad::QueryStats,
    found_provider: bool,
}

async fn register(
    provider: &mut Swarm<Behaviour>,
    listener_peer: PeerId,
    listener_addr: Multiaddr,
    key: &kad::RecordKey,
) -> Result<(), Box<dyn Error>> {
    provider
        .behaviour_mut()
        .kad
        .add_address(&listener_peer, listener_addr.clone());
    let dial = DialOpts::peer_id(listener_peer)
        .addresses(vec![listener_addr])
        .build();
    let connection_id = dial.connection_id();
    provider.dial(dial)?;
    let mut listening = false;
    let mut connected = false;
    let mut provide_id = None;
    loop {
        match provider.select_next_some().await {
            SwarmEvent::NewListenAddr { address, .. } => {
                provider.add_external_address(address);
                listening = true;
            }
            SwarmEvent::ConnectionEstablished {
                peer_id,
                connection_id: id,
                ..
            } if id == connection_id && peer_id == listener_peer => {
                connected = true;
            }
            SwarmEvent::OutgoingConnectionError {
                connection_id: id,
                error,
                ..
            } if id == connection_id => {
                return Err(format!("provider connection to listener failed: {error}").into());
            }
            SwarmEvent::ListenerClosed {
                reason: Err(error), ..
            } => {
                return Err(format!("provider listener closed: {error}").into());
            }
            SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                id,
                result: kad::QueryResult::StartProviding(result),
                stats,
                step,
            })) if Some(id) == provide_id => {
                let published = result?;
                if published.key != *key
                    || !step.last
                    || stats.num_requests() == 0
                    || stats.num_successes() == 0
                {
                    return Err(
                        "provider registration lacks matching key and successful network query"
                            .into(),
                    );
                }
                // This is start_providing completion, not an ADD_PROVIDER wire ACK.
                // Keep polling P below so its queued publication can reach S.
                return Ok(());
            }
            _ => {}
        }
        if listening && connected && provide_id.is_none() {
            provide_id = Some(provider.behaviour_mut().kad.start_providing(key.clone())?);
        }
    }
}

async fn find(
    provider: &mut Swarm<Behaviour>,
    querier: &mut Swarm<Behaviour>,
    key: &kad::RecordKey,
) -> Result<LookupEvidence, Box<dyn Error>> {
    let provider_peer = *provider.local_peer_id();
    if !querier
        .behaviour_mut()
        .kad
        .store_mut()
        .providers(key)
        .is_empty()
    {
        return Err("fresh querier unexpectedly has a local provider record".into());
    }
    let mut query_id = querier.behaviour_mut().kad.get_providers(key.clone());
    eprintln!("rust provider lookup started: attempt=1, id={query_id:?}");
    let mut matched_query_id = None;
    let mut query_complete = false;
    let mut query_stats = kad::QueryStats::empty();
    let mut provider_count = 0;
    let mut connection_id = None;
    let mut discovered_address = None;
    let mut saw_provider = false;
    let mut attempts = Vec::new();
    let mut retry_pending = false;
    let backoff = tokio::time::sleep(Duration::from_millis(100));
    tokio::pin!(backoff);
    loop {
        tokio::select! {
            _ = &mut backoff, if retry_pending => {
                // The outer operation deadline also bounds this delay. Keep both
                // swarms polled below while P's publication reaches S.
                query_id = querier.behaviour_mut().kad.get_providers(key.clone());
                eprintln!("rust provider lookup started: attempt={}, id={query_id:?}", attempts.len() + 1);
                matched_query_id = None;
                query_complete = false;
                query_stats = kad::QueryStats::empty();
                provider_count = 0;
                connection_id = None;
                discovered_address = None;
                saw_provider = false;
                retry_pending = false;
            },
            event = provider.select_next_some() => match event {
                SwarmEvent::NewListenAddr { address, .. } => provider.add_external_address(address),
                SwarmEvent::ListenerClosed { reason: Err(error), .. } => {
                    return Err(format!("provider listener closed during lookup: {error}").into());
                }
                _ => {}
            },
            event = querier.select_next_some() => match event {
                SwarmEvent::Behaviour(BehaviourEvent::Kad(kad::Event::OutboundQueryProgressed {
                    id, result: kad::QueryResult::GetProviders(result), stats, step,
                })) if id == query_id => {
                    // Each progress event contains a cumulative snapshot for this
                    // query. Replacing it avoids double-counting earlier requests.
                    query_stats = stats;
                    match result? {
                        kad::GetProvidersOk::FoundProviders { key: found_key, providers } => {
                            if found_key != *key {
                                return Err("matching provider query returned a different key".into());
                            }
                            saw_provider |= !providers.is_empty();
                            if providers.contains(&provider_peer) && matched_query_id.is_none() {
                                if stats.num_requests() == 0 || stats.num_successes() == 0 {
                                    return Err("FoundProviders lacks successful network requests".into());
                                }
                                // rust-libp2p 22fb4c78: pending outbound connections
                                // read active-query addresses. Do not poll Q again
                                // before this address-free dial consumes them.
                                let dial = DialOpts::peer_id(provider_peer).build();
                                connection_id = Some(dial.connection_id());
                                querier.dial(dial)?;
                                matched_query_id = Some(id);
                                provider_count = providers.len();
                            }
                        }
                        kad::GetProvidersOk::FinishedWithNoAdditionalRecord { .. } => {}
                    }
                    query_complete = step.last;
                    if query_complete {
                        eprintln!("rust provider lookup attempt: id={id:?}, stats={stats:?}, found_p={}",
                            matched_query_id.is_some());
                        attempts.push(LookupAttempt {
                            query_id: id,
                            stats,
                            found_provider: matched_query_id.is_some(),
                        });
                        if matched_query_id.is_none() {
                            if saw_provider || stats.num_requests() == 0 || stats.num_successes() == 0
                                || stats.num_failures() != 0
                            {
                                return Err("provider query without P was not a successful empty network lookup".into());
                            }
                            // ADD_PROVIDER has no ACK. Only a successful empty
                            // response may race persistence; errors are not retried.
                            backoff.as_mut().reset(tokio::time::Instant::now() + Duration::from_millis(100));
                            retry_pending = true;
                        }
                    }
                }
                SwarmEvent::ConnectionEstablished { peer_id, connection_id: id, endpoint, .. }
                    if Some(id) == connection_id =>
                {
                    if peer_id != provider_peer {
                        return Err("discovery dial authenticated a different provider".into());
                    }
                    let ConnectedPoint::Dialer { address, .. } = endpoint else {
                        return Err("discovery proof requires an outbound connection".into());
                    };
                    discovered_address = Some(address);
                }
                SwarmEvent::OutgoingConnectionError { connection_id: id, error, .. }
                    if Some(id) == connection_id =>
                {
                    return Err(format!("address-free provider discovery dial failed: {error}").into());
                }
                SwarmEvent::NewListenAddr { address, .. } => querier.add_external_address(address),
                SwarmEvent::ListenerClosed { reason: Err(error), .. } => {
                    return Err(format!("querier listener closed: {error}").into());
                }
                _ => {}
            },
        }
        if query_complete && discovered_address.is_some() {
            if query_stats.num_requests() == 0 || query_stats.num_successes() == 0 {
                return Err("terminal provider query lacks successful network requests".into());
            }
            return Ok(LookupEvidence {
                query_id,
                matched_query_id: matched_query_id.ok_or("missing FoundProviders query binding")?,
                stats: query_stats,
                provider_count,
                connection_id: connection_id.ok_or("missing discovery connection binding")?,
                address: discovered_address.ok_or("missing authenticated discovery address")?,
                attempts,
            });
        }
    }
}

async fn lookup(opts: &Options) -> Result<serde_json::Value, Box<dyn Error>> {
    let listener_peer: PeerId = opts.peer_id.parse()?;
    let listener_addr: Multiaddr = opts.addr.parse()?;
    if let Some(Protocol::P2p(peer)) = listener_addr.iter().last()
        && peer != listener_peer
    {
        return Err("listener address peer disagrees with --peer-id".into());
    }
    let listener_addr = transport_addr(listener_addr);
    let mut provider = new_swarm(opts).await?;
    let provider_peer = *provider.local_peer_id();
    if provider_peer == listener_peer {
        return Err("provider and listener identities must differ".into());
    }
    // PeerId bytes already encode a multihash, unique to P's fresh identity.
    let key_bytes = provider_peer.to_bytes();
    let key = kad::RecordKey::new(&key_bytes);
    let key_hex = key_bytes
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect::<String>();
    register(&mut provider, listener_peer, listener_addr.clone(), &key).await?;

    // Construct Q only after registration: separate identity, routing state and
    // MemoryStore. Its only configured peer/address is S, never P's listen address.
    let mut querier = new_swarm(opts).await?;
    let querier_peer = *querier.local_peer_id();
    if querier_peer == provider_peer || querier_peer == listener_peer {
        return Err("querier, provider and listener identities must be independent".into());
    }
    querier
        .behaviour_mut()
        .kad
        .add_address(&listener_peer, listener_addr);
    let evidence = find(&mut provider, &mut querier, &key).await?;
    Ok(json!({
        "implementation": "rust",
        "role": "dialer",
        "scenario": "dht_provide_find_provider",
        "status": "ok",
        "provider_count": evidence.provider_count,
        "provider_query_attempts": evidence.attempts.iter().map(|attempt| json!({
            "query_id": format!("{:?}", attempt.query_id),
            "num_requests": attempt.stats.num_requests(),
            "num_successes": attempt.stats.num_successes(),
            "num_failures": attempt.stats.num_failures(),
            "found_provider": attempt.found_provider
        })).collect::<Vec<_>>(),
        "network_proof": {
            "schema": "forge.libp2p.provider-network-proof.v1",
            "implementation": "rust",
            "provider_peer": provider_peer.to_string(),
            "querier_peer": querier_peer.to_string(),
            "listener_peer": listener_peer.to_string(),
            "provider_key": key_hex,
            "key_binding": {
                "kind": "provider_identity_multihash",
                "provider_peer": provider_peer.to_string(),
                "provider_key": key_hex,
                "derived_per_run": true
            },
            "provider_registration": {
                "api": "start_providing",
                "succeeded": true,
                "provider_peer": provider_peer.to_string(),
                "provider_key": key_hex
            },
            "api_lookup": {
                "api": "get_providers",
                "succeeded": true,
                "querier_peer": querier_peer.to_string(),
                "returned_provider_peer": provider_peer.to_string(),
                "provider_key": key_hex
            },
            "address_proof": {
                "source": "kad_discovery_peer_id_dial",
                "provider_peer": provider_peer.to_string(),
                "address": evidence.address.to_string(),
                "connection_id": format!("{:?}", evidence.connection_id),
                "immediate_after_found_providers": true,
                "authenticated_peer": provider_peer.to_string()
            },
            "protocol_proof": {
                "protocol": KAD_PROTOCOL,
                "derivation": "sole_configured_ipfs_kad_1_0_0_successful_query_stats",
                "successful_query": true
            },
            "source_query_proof": {
                "kind": "rust_kad_query_stats",
                "query_id": format!("{:?}", evidence.query_id),
                "matched_query_id": format!("{:?}", evidence.matched_query_id),
                "provider_key": key_hex,
                "found_provider_peer": provider_peer.to_string(),
                "num_requests": evidence.stats.num_requests(),
                "num_successes": evidence.stats.num_successes(),
                "derivation": "matching_query_id_key_found_providers_query_stats"
            },
            "wire_proof": null
        }
    }))
}

pub(super) async fn run(opts: &Options) -> Result<(), Box<dyn Error>> {
    let result = tokio::time::timeout(Duration::from_secs(30), lookup(opts))
        .await
        .map_err(|_| "timed out waiting for independent Kademlia provider proof")??;
    write_json(&opts.result_file, result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        borrow::Cow,
        sync::{
            Arc,
            atomic::{AtomicUsize, Ordering},
        },
    };

    use futures::channel::oneshot;
    use libp2p::{StreamProtocol, SwarmBuilder, identity};

    // Only S's first read is hidden. Storage and wire responses remain real;
    // neither P's record nor its addresses are ever installed in Q by the test.
    struct EmptyFirstStore {
        inner: kad::store::MemoryStore,
        key: kad::RecordKey,
        reads: Arc<AtomicUsize>,
        persisted: Option<oneshot::Sender<()>>,
    }

    impl RecordStore for EmptyFirstStore {
        type RecordsIter<'a> = <kad::store::MemoryStore as RecordStore>::RecordsIter<'a>;
        type ProvidedIter<'a> = <kad::store::MemoryStore as RecordStore>::ProvidedIter<'a>;

        fn get(&self, key: &kad::RecordKey) -> Option<Cow<'_, kad::Record>> {
            self.inner.get(key)
        }

        fn put(&mut self, record: kad::Record) -> kad::store::Result<()> {
            self.inner.put(record)
        }

        fn remove(&mut self, key: &kad::RecordKey) {
            self.inner.remove(key);
        }

        fn records(&self) -> Self::RecordsIter<'_> {
            self.inner.records()
        }

        fn add_provider(&mut self, record: kad::ProviderRecord) -> kad::store::Result<()> {
            let matching = record.key == self.key;
            self.inner.add_provider(record)?;
            if matching && let Some(persisted) = self.persisted.take() {
                let _ = persisted.send(());
            }
            Ok(())
        }

        fn providers(&self, key: &kad::RecordKey) -> Vec<kad::ProviderRecord> {
            let records = self.inner.providers(key);
            if key == &self.key && self.reads.fetch_add(1, Ordering::SeqCst) == 0 {
                assert!(
                    !records.is_empty(),
                    "P must really publish before the forced empty response"
                );
                Vec::new()
            } else {
                records
            }
        }

        fn provided(&self) -> Self::ProvidedIter<'_> {
            self.inner.provided()
        }

        fn remove_provider(&mut self, key: &kad::RecordKey, peer: &PeerId) {
            self.inner.remove_provider(key, peer);
        }
    }

    #[tokio::test]
    async fn provider_lookup_does_not_retry_empty_query_without_network_success() {
        tokio::time::timeout(Duration::from_secs(5), async {
            let opts = Options {
                scenario: "dht_provide_find_provider".into(),
                transport: "quic".into(),
                ..Options::default()
            };
            let mut provider = new_swarm(&opts).await.expect("provider swarm");
            let key = kad::RecordKey::new(&provider.local_peer_id().to_bytes());
            let mut querier = new_swarm(&opts).await.expect("querier swarm");
            // An unseeded Q returns a real empty result with zero network requests.
            // It must fail, not enter the publication-propagation retry path.
            let result = find(&mut provider, &mut querier, &key).await;
            let error = result.err().expect("empty local lookup must not retry");
            assert!(
                error
                    .to_string()
                    .contains("not a successful empty network lookup")
            );
        })
        .await
        .expect("bounded unseeded querier rejection");
    }

    #[tokio::test]
    async fn provider_lookup_retries_successful_empty_query_with_fresh_query_stats() {
        tokio::time::timeout(Duration::from_secs(15), async {
            let opts = Options {
                scenario: "dht_provide_find_provider".into(),
                transport: "quic".into(),
                ..Options::default()
            };
            let mut provider = new_swarm(&opts).await.expect("provider swarm");
            let provider_peer = *provider.local_peer_id();
            let key = kad::RecordKey::new(&provider_peer.to_bytes());
            let reads = Arc::new(AtomicUsize::new(0));
            let (persisted, mut persistence) = oneshot::channel();
            let listener_key = identity::Keypair::generate_ed25519();
            let listener_peer = listener_key.public().to_peer_id();
            assert_ne!(provider_peer, listener_peer);
            let store = EmptyFirstStore {
                inner: kad::store::MemoryStore::new(listener_peer),
                key: key.clone(),
                reads: Arc::clone(&reads),
                persisted: Some(persisted),
            };
            let mut listener = SwarmBuilder::with_existing_identity(listener_key)
                .with_tokio()
                .with_quic()
                .with_behaviour(|_| {
                    let mut config = kad::Config::new(StreamProtocol::new(KAD_PROTOCOL));
                    // S must not expose P as a closer peer during the empty
                    // reply, which would allow that same query to ask P directly.
                    config.set_kbucket_inserts(kad::BucketInserts::Manual);
                    config.set_periodic_bootstrap_interval(None);
                    let mut kad = kad::Behaviour::with_config(listener_peer, store, config);
                    kad.set_mode(Some(kad::Mode::Server));
                    kad
                })
                .expect("listener behaviour")
                .build();
            listener.listen_on("/ip4/127.0.0.1/udp/0/quic-v1".parse().unwrap()).unwrap();
            let listener_addr = loop {
                if let SwarmEvent::NewListenAddr { address, .. } = listener.select_next_some().await {
                    listener.add_external_address(address.clone());
                    break address;
                }
            };
            let exercise = async {
                register(&mut provider, listener_peer, listener_addr.clone(), &key).await?;
                // Wait for S's real ADD_PROVIDER store transition, still polling P.
                loop {
                    tokio::select! {
                        stored = &mut persistence => { stored?; break; },
                        event = provider.select_next_some() => {
                            if let SwarmEvent::NewListenAddr { address, .. } = event {
                                provider.add_external_address(address);
                            }
                        },
                    }
                }
                let mut querier = new_swarm(&opts).await?;
                assert_ne!(*querier.local_peer_id(), provider_peer);
                assert_ne!(*querier.local_peer_id(), listener_peer);
                querier.behaviour_mut().kad.add_address(&listener_peer, listener_addr);
                find(&mut provider, &mut querier, &key).await
            };
            tokio::pin!(exercise);
            let mut empty_wire_replies = 0;
            let evidence = loop {
                tokio::select! {
                    result = &mut exercise => break result.expect("independent provider lookup"),
                    event = listener.select_next_some() => match event {
                        SwarmEvent::Behaviour(kad::Event::InboundRequest {
                            request: kad::InboundRequest::GetProvider { num_provider_peers: 0, .. },
                        }) => empty_wire_replies += 1,
                        SwarmEvent::ListenerClosed { reason: Err(error), .. } => panic!("listener failed: {error}"),
                        _ => {},
                    },
                }
            };
            assert_eq!(empty_wire_replies, 1);
            assert_eq!(reads.load(Ordering::SeqCst), 2);
            assert_eq!(evidence.attempts.len(), 2);
            let first = &evidence.attempts[0];
            let last = &evidence.attempts[1];
            assert!(!first.found_provider);
            assert!(first.stats.num_requests() > 0);
            assert!(first.stats.num_successes() > 0);
            assert_eq!(first.stats.num_failures(), 0);
            assert_ne!(first.query_id, last.query_id);
            assert!(last.found_provider);
            assert_eq!(evidence.query_id, last.query_id);
            assert_eq!(evidence.matched_query_id, last.query_id);
            assert_eq!(evidence.stats, last.stats);
            assert!(evidence.stats.num_successes() > 0);
            assert!(evidence.stats.num_requests() < first.stats.num_requests() + last.stats.num_requests());
            assert!(!evidence.address.is_empty());
        }).await.expect("bounded empty-first provider regression");
    }
}

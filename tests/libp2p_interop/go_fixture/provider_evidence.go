package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"time"

	cid "github.com/ipfs/go-cid"
	kadpb "github.com/libp2p/go-libp2p-kad-dht/pb"
	"github.com/libp2p/go-libp2p/core/network"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/core/protocol"
	mh "github.com/multiformats/go-multihash"
	"google.golang.org/protobuf/proto"
)

const getProvidersWireTimeout = 5 * time.Second

func providerCIDForIdentity(identity peer.ID) (cid.Cid, string, error) {
	digest, err := mh.Cast([]byte(identity))
	if err != nil {
		return cid.Undef, "", err
	}
	return cid.NewCidV1(cid.Raw, digest), hex.EncodeToString(digest), nil
}

func providerLivenessPoll(provider *fixtureHost, listener peer.ID) (chan<- struct{}, <-chan struct{}, <-chan error) {
	stop := make(chan struct{})
	done := make(chan struct{})
	pollErr := make(chan error, 1)
	go func() {
		defer close(done)
		ticker := time.NewTicker(25 * time.Millisecond)
		defer ticker.Stop()
		for {
			if provider.Network().Connectedness(listener) != network.Connected {
				select {
				case pollErr <- fmt.Errorf("provider lost its listener connection while querier lookup was active"):
				case <-stop:
				}
				return
			}
			select {
			case <-stop:
				return
			case <-ticker.C:
			}
		}
	}()
	return stop, done, pollErr
}

func providerLookupFromQuerier(ctx context.Context, provider, querier *fixtureHost, key cid.Cid,
	expected, listener peer.ID) (peer.AddrInfo, int, error) {
	stopLiveness, livenessDone, livenessErr := providerLivenessPoll(provider, listener)
	defer func() {
		close(stopLiveness)
		<-livenessDone
	}()
	deadline := time.Now().Add(10 * time.Second)
	totalQueryEvents := 0
	for time.Now().Before(deadline) {
		select {
		case err := <-livenessErr:
			return peer.AddrInfo{}, totalQueryEvents, err
		default:
		}
		attemptCtx, cancel := context.WithTimeout(ctx, 2*time.Second)
		var matched peer.AddrInfo
		queryEvents, queryErr := dhtQueryEvidence(attemptCtx, func(queryCtx context.Context) error {
			for provider := range querier.kad.FindProvidersAsync(queryCtx, key, 10) {
				if provider.ID == expected && len(provider.Addrs) != 0 {
					matched = provider
				}
			}
			return nil
		})
		cancel()
		totalQueryEvents += queryEvents
		select {
		case err := <-livenessErr:
			return peer.AddrInfo{}, totalQueryEvents, err
		default:
		}
		if queryErr != nil {
			return peer.AddrInfo{}, totalQueryEvents, fmt.Errorf("FindProvidersAsync query: %w", queryErr)
		}
		if matched.ID == expected {
			if totalQueryEvents == 0 {
				return peer.AddrInfo{}, totalQueryEvents, fmt.Errorf("FindProvidersAsync returned %s without a routing query event", expected)
			}
			return matched, totalQueryEvents, nil
		}
		time.Sleep(50 * time.Millisecond)
	}
	return peer.AddrInfo{}, totalQueryEvents, fmt.Errorf("FindProvidersAsync did not return provider %s", expected)
}

// explicitGetProvidersWireEvidence confirms the provider record over a real
// DHT RPC. It is independent of FindProvidersAsync and must not be interpreted
// as telemetry for the internal API lookup.
func explicitGetProvidersWireEvidence(ctx context.Context, querier *fixtureHost, listener, expected peer.ID,
	key cid.Cid) (provider peer.AddrInfo, selectedProtocol string, err error) {
	return explicitGetProvidersWireEvidenceWithTimeout(ctx, querier, listener, expected, key, getProvidersWireTimeout)
}

func explicitGetProvidersWireEvidenceWithTimeout(ctx context.Context, querier *fixtureHost, listener, expected peer.ID,
	key cid.Cid, timeout time.Duration) (provider peer.AddrInfo, selectedProtocol string, err error) {
	if timeout <= 0 {
		return peer.AddrInfo{}, "", fmt.Errorf("GET_PROVIDERS wire timeout must be positive")
	}
	wireCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	stream, err := querier.NewStream(wireCtx, listener, protocol.ID("/ipfs/kad/1.0.0"))
	if err != nil {
		return peer.AddrInfo{}, "", fmt.Errorf("open GET_PROVIDERS stream to listener: %w", err)
	}
	defer func() {
		if err != nil {
			if resetErr := stream.Reset(); resetErr != nil {
				err = errors.Join(err, fmt.Errorf("reset GET_PROVIDERS stream: %w", resetErr))
			}
			return
		}
		if closeErr := stream.Close(); closeErr != nil {
			err = fmt.Errorf("close GET_PROVIDERS stream: %w", closeErr)
		}
	}()
	deadline, hasDeadline := wireCtx.Deadline()
	if !hasDeadline {
		return peer.AddrInfo{}, "", fmt.Errorf("GET_PROVIDERS wire context has no deadline")
	}
	if deadlineErr := stream.SetDeadline(deadline); deadlineErr != nil {
		return peer.AddrInfo{}, "", fmt.Errorf("set GET_PROVIDERS stream deadline: %w", deadlineErr)
	}
	selectedProtocol = string(stream.Protocol())
	if selectedProtocol != "/ipfs/kad/1.0.0" {
		return peer.AddrInfo{}, "", fmt.Errorf("GET_PROVIDERS selected %q, expected /ipfs/kad/1.0.0", selectedProtocol)
	}
	request, marshalErr := proto.Marshal(kadpb.NewMessage(kadpb.Message_GET_PROVIDERS, key.Hash(), 0))
	if marshalErr != nil {
		return peer.AddrInfo{}, "", fmt.Errorf("encode GET_PROVIDERS request: %w", marshalErr)
	}
	if writeErr := writeFrame(stream, request); writeErr != nil {
		return peer.AddrInfo{}, "", fmt.Errorf("write GET_PROVIDERS request: %w", writeErr)
	}
	responseBytes, readErr := readFrame(bufio.NewReader(stream))
	if readErr != nil {
		return peer.AddrInfo{}, "", fmt.Errorf("read GET_PROVIDERS response: %w", readErr)
	}
	response := &kadpb.Message{}
	if unmarshalErr := proto.Unmarshal(responseBytes, response); unmarshalErr != nil {
		return peer.AddrInfo{}, "", fmt.Errorf("decode GET_PROVIDERS response: %w", unmarshalErr)
	}
	if response.GetType() != kadpb.Message_GET_PROVIDERS {
		return peer.AddrInfo{}, "", fmt.Errorf("GET_PROVIDERS response type %s", response.GetType())
	}
	// rust-libp2p omits the response key. This stream carries one request, so
	// an absent key remains correlated while any non-empty mismatch is rejected.
	if responseKey := response.GetKey(); len(responseKey) != 0 && !bytes.Equal(responseKey, key.Hash()) {
		return peer.AddrInfo{}, "", fmt.Errorf("GET_PROVIDERS response key did not bind the provider lookup key")
	}
	for _, candidate := range kadpb.PBPeersToPeerInfos(response.GetProviderPeers()) {
		if candidate.ID == expected && len(candidate.Addrs) != 0 {
			return *candidate, selectedProtocol, nil
		}
	}
	return peer.AddrInfo{}, "", fmt.Errorf("GET_PROVIDERS response did not contain provider %s with addresses", expected)
}

func commonProviderAddress(apiProvider, wireProvider peer.AddrInfo) (string, error) {
	if apiProvider.ID != wireProvider.ID {
		return "", fmt.Errorf("API and wire provider identities differ: %s != %s", apiProvider.ID, wireProvider.ID)
	}
	for _, apiAddress := range apiProvider.Addrs {
		for _, wireAddress := range wireProvider.Addrs {
			if apiAddress.String() == wireAddress.String() {
				return apiAddress.String(), nil
			}
		}
	}
	return "", fmt.Errorf("API and wire provider results have no common address for %s", apiProvider.ID)
}

// ensureQuerierDoesNotKnowProvider checks Q's local state only. ProviderStore
// access is not a network lookup and therefore cannot establish the evidence it
// is guarding.
func ensureQuerierDoesNotKnowProvider(ctx context.Context, querier *fixtureHost, provider peer.ID, key cid.Cid) error {
	if len(querier.Peerstore().Addrs(provider)) != 0 || len(querier.Network().ConnsToPeer(provider)) != 0 {
		return fmt.Errorf("querier learned provider %s before the lookup", provider)
	}
	providers, err := querier.kad.ProviderStore().GetProviders(ctx, key.Hash())
	if err != nil {
		return fmt.Errorf("read querier provider store before lookup: %w", err)
	}
	for _, candidate := range providers {
		if candidate.ID == provider {
			return fmt.Errorf("querier stored provider %s before the lookup", provider)
		}
	}
	return nil
}

func independentProviderEvidence(ctx context.Context, provider *fixtureHost, listener *peer.AddrInfo,
	transport, pnetKeyFile, dnsServer string) (evidence map[string]any, err error) {
	if provider.ID() == listener.ID {
		return nil, fmt.Errorf("provider and listener identities must differ")
	}
	if provider.Network().Connectedness(listener.ID) != network.Connected {
		return nil, fmt.Errorf("provider is not connected to the listener before Provide")
	}
	addDHTPeer(provider, listener)
	key, keyText, err := providerCIDForIdentity(provider.ID())
	if err != nil {
		return nil, fmt.Errorf("derive provider key: %w", err)
	}
	if err := provider.kad.Provide(ctx, key, true); err != nil {
		return nil, fmt.Errorf("provider Provide: %w", err)
	}
	if provider.Network().Connectedness(listener.ID) != network.Connected {
		return nil, fmt.Errorf("provider lost its listener connection before querier lookup")
	}
	querier, err := newHost(transport, pnetKeyFile, dnsServer)
	if err != nil {
		return nil, fmt.Errorf("create fresh querier: %w", err)
	}
	defer func() {
		if closeErr := querier.Close(); closeErr != nil {
			if err != nil {
				err = fmt.Errorf("%w; querier cleanup failed: %v", err, closeErr)
			} else {
				err = fmt.Errorf("querier cleanup failed: %w", closeErr)
			}
		}
	}()
	if provider.ID() == querier.ID() || querier.ID() == listener.ID {
		return nil, fmt.Errorf("provider, querier and listener identities must be pairwise distinct")
	}
	if err := querier.Connect(ctx, *listener); err != nil {
		return nil, fmt.Errorf("connect fresh querier to listener: %w", err)
	}
	addDHTPeer(querier, listener)
	if err := ensureQuerierDoesNotKnowProvider(ctx, querier, provider.ID(), key); err != nil {
		return nil, err
	}
	discovered, queryEvents, err := providerLookupFromQuerier(
		ctx, provider, querier, key, provider.ID(), listener.ID)
	if err != nil {
		return nil, err
	}
	if provider.Network().Connectedness(listener.ID) != network.Connected {
		return nil, fmt.Errorf("provider was not alive through the querier lookup")
	}
	if len(discovered.Addrs) == 0 {
		return nil, fmt.Errorf("FindProvidersAsync returned provider %s without addresses", provider.ID())
	}
	wireProvider, selectedProtocol, err := explicitGetProvidersWireEvidence(
		ctx, querier, listener.ID, provider.ID(), key)
	if err != nil {
		return nil, err
	}
	address, err := commonProviderAddress(discovered, wireProvider)
	if err != nil {
		return nil, err
	}
	providerPeer := provider.ID().String()
	querierPeer := querier.ID().String()
	listenerPeer := listener.ID.String()
	return map[string]any{
		"schema":         "forge.libp2p.provider-network-proof.v1",
		"implementation": "go",
		"provider_peer":  providerPeer,
		"querier_peer":   querierPeer,
		"listener_peer":  listenerPeer,
		"provider_key":   keyText,
		"key_binding": map[string]any{
			"kind":            "provider_identity_multihash",
			"provider_peer":   providerPeer,
			"provider_key":    keyText,
			"derived_per_run": true,
		},
		"provider_registration": map[string]any{
			"api":           "Provide",
			"succeeded":     true,
			"provider_peer": providerPeer,
			"provider_key":  keyText,
		},
		"api_lookup": map[string]any{
			"api":                    "FindProvidersAsync",
			"succeeded":              true,
			"querier_peer":           querierPeer,
			"returned_provider_peer": discovered.ID.String(),
			"provider_key":           keyText,
		},
		"address_proof": map[string]any{
			"source":        "find_providers_result",
			"provider_peer": discovered.ID.String(),
			"address":       address,
		},
		"protocol_proof": map[string]any{
			"protocol":               "/ipfs/kad/1.0.0",
			"derivation":             "explicit_get_providers_wire_reply_selected_protocol",
			"successful_query":       true,
			"opened_stream_protocol": selectedProtocol,
		},
		"source_query_proof": map[string]any{
			"kind":                 "go_routing_query_events",
			"querier_peer":         querierPeer,
			"provider_key":         keyText,
			"sending_query_events": queryEvents,
		},
		"wire_proof": map[string]any{
			"kind":                       "go_get_providers_wire_reply",
			"explicit_wire_confirmation": true,
			"listener_peer":              listenerPeer,
			"returned_provider_peer":     wireProvider.ID.String(),
			"provider_key":               keyText,
			"address":                    address,
			"opened_stream_protocol":     selectedProtocol,
		},
	}, nil
}

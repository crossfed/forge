package main

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"fmt"
	"net"
	"strings"
	"sync"
	"testing"
	"time"

	ds "github.com/ipfs/go-datastore"
	dssync "github.com/ipfs/go-datastore/sync"
	libp2p "github.com/libp2p/go-libp2p"
	kad "github.com/libp2p/go-libp2p-kad-dht"
	kadpb "github.com/libp2p/go-libp2p-kad-dht/pb"
	"github.com/libp2p/go-libp2p-kad-dht/records"
	"github.com/libp2p/go-libp2p/core/network"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/core/protocol"
	"google.golang.org/protobuf/proto"
)

type recordingProviderStore struct {
	records.ProviderStore
	mu          sync.Mutex
	lookupKeys  [][]byte
	lookupSizes []int
}

func (s *recordingProviderStore) GetProviders(ctx context.Context, key []byte) ([]peer.AddrInfo, error) {
	providers, err := s.ProviderStore.GetProviders(ctx, key)
	s.mu.Lock()
	s.lookupKeys = append(s.lookupKeys, append([]byte(nil), key...))
	s.lookupSizes = append(s.lookupSizes, len(providers))
	s.mu.Unlock()
	return providers, err
}

func (s *recordingProviderStore) lookups() ([][]byte, []int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	keys := make([][]byte, len(s.lookupKeys))
	for index, key := range s.lookupKeys {
		keys[index] = append([]byte(nil), key...)
	}
	return keys, append([]int(nil), s.lookupSizes...)
}

func newProviderEvidenceTestHost(t *testing.T, wrapStore func(records.ProviderStore) records.ProviderStore,
	dhtOptions ...kad.Option) *fixtureHost {
	t.Helper()
	h, err := libp2p.New(libp2p.ListenAddrStrings("/ip4/127.0.0.1/tcp/0"))
	if err != nil {
		t.Fatal(err)
	}
	store := dssync.MutexWrap(ds.NewMapDatastore())
	providerManager, err := records.NewProviderManager(context.Background(), h.ID(), h.Peerstore(), store)
	if err != nil {
		_ = h.Close()
		t.Fatal(err)
	}
	var providerStore records.ProviderStore = providerManager
	if wrapStore != nil {
		providerStore = wrapStore(providerStore)
	}
	options := []kad.Option{
		kad.Mode(kad.ModeServer),
		kad.DisableAutoRefresh(),
		kad.Datastore(store),
		kad.ProviderStore(providerStore),
	}
	options = append(options, dhtOptions...)
	dht, err := kad.New(context.Background(), h, options...)
	if err != nil {
		_ = providerStore.Close()
		_ = h.Close()
		t.Fatal(err)
	}
	result := &fixtureHost{Host: h, kad: dht, dhtStore: store}
	t.Cleanup(func() {
		if closeErr := result.Close(); closeErr != nil {
			t.Errorf("close test DHT host: %v", closeErr)
		}
	})
	return result
}

func waitForListenerProviderRecord(ctx context.Context, listener *fixtureHost, provider peer.ID, key []byte) error {
	for {
		if err := ctx.Err(); err != nil {
			return fmt.Errorf("listener did not persist P's ADD_PROVIDER record before Q started: %w", err)
		}
		providers, err := listener.kad.ProviderStore().GetProviders(ctx, key)
		if err != nil {
			return fmt.Errorf("read listener provider store: %w", err)
		}
		for _, candidate := range providers {
			if candidate.ID == provider && len(candidate.Addrs) != 0 {
				return nil
			}
		}
		select {
		case <-ctx.Done():
			return fmt.Errorf("listener did not persist P's ADD_PROVIDER record before Q started: %w", ctx.Err())
		case <-time.After(25 * time.Millisecond):
		}
	}
}

func TestProviderLookupAcceptsPrewarmedDHTStream(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	provider := newProviderEvidenceTestHost(t, nil)
	var listenerStore *recordingProviderStore
	listener := newProviderEvidenceTestHost(t, func(store records.ProviderStore) records.ProviderStore {
		listenerStore = &recordingProviderStore{ProviderStore: store}
		return listenerStore
	}, kad.RoutingTableFilter(func(_ any, candidate peer.ID) bool {
		return candidate != provider.ID()
	}))
	listenerInfo := peer.AddrInfo{ID: listener.ID(), Addrs: listener.Addrs()}
	providerKey, _, err := providerCIDForIdentity(provider.ID())
	if err != nil {
		t.Fatal(err)
	}
	if err := provider.Connect(ctx, listenerInfo); err != nil {
		t.Fatal(err)
	}
	addDHTPeer(provider, &listenerInfo)
	if err := provider.kad.Provide(ctx, providerKey, true); err != nil {
		t.Fatal(err)
	}
	if err := waitForListenerProviderRecord(ctx, listener, provider.ID(), providerKey.Hash()); err != nil {
		t.Fatal(err)
	}
	lookupKeysBeforePrewarm, _ := listenerStore.lookups()

	// S preserves the actual provider record but is configured before P
	// connects to reject P from its routing table. A different-key prewarm then
	// cannot disclose P as a closer peer and teach Q P's address before lookup.
	if listener.kad.RoutingTable().Find(provider.ID()) != "" {
		t.Fatalf("listener routing table admitted filtered provider %s", provider.ID())
	}

	querier := newProviderEvidenceTestHost(t, nil)
	if err := querier.Connect(ctx, listenerInfo); err != nil {
		t.Fatal(err)
	}
	addDHTPeer(querier, &listenerInfo)
	emptyKey, _, err := providerCIDForIdentity(querier.ID())
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(emptyKey.Hash(), providerKey.Hash()) {
		t.Fatal("prewarm key must differ from P's provider key")
	}

	// This empty-key public lookup opens Q's reusable DHT stream to S. The next
	// provider-key lookup must succeed even when that stream is cached.
	prewarmEvents, err := dhtQueryEvidence(ctx, func(queryCtx context.Context) error {
		for found := range querier.kad.FindProvidersAsync(queryCtx, emptyKey, 1) {
			t.Fatalf("prewarm lookup unexpectedly returned %s", found.ID)
		}
		return nil
	})
	if err != nil || prewarmEvents == 0 {
		t.Fatalf("prewarm FindProvidersAsync did not issue a query: events=%d err=%v", prewarmEvents, err)
	}
	if err := ensureQuerierDoesNotKnowProvider(ctx, querier, provider.ID(), providerKey); err != nil {
		t.Fatal(err)
	}

	found, events, err := providerLookupFromQuerier(ctx, provider, querier, providerKey, provider.ID(), listener.ID())
	if err != nil {
		t.Fatal(err)
	}
	if found.ID != provider.ID() || len(found.Addrs) == 0 || events == 0 {
		t.Fatalf("successful lookup did not return P with addresses and a Q query: found=%+v events=%d", found, events)
	}

	keys, sizes := listenerStore.lookups()
	firstWireLookup := len(lookupKeysBeforePrewarm)
	if len(sizes) < firstWireLookup+2 || sizes[firstWireLookup] != 0 || sizes[firstWireLookup+1] == 0 {
		t.Fatalf("expected empty prewarm then provider-bearing GET_PROVIDERS replies, got sizes=%v", sizes)
	}
	if !bytes.Equal(keys[firstWireLookup], emptyKey.Hash()) || !bytes.Equal(keys[firstWireLookup+1], providerKey.Hash()) {
		t.Fatalf("GET_PROVIDERS replies were not bound to the prewarm and provider keys: %x", keys)
	}
}

func TestExplicitGetProvidersWireEvidenceResponseKeyCorrelation(t *testing.T) {
	for _, test := range []struct {
		name      string
		emptyKey  bool
		wantError bool
	}{
		{name: "omitted response key is accepted", emptyKey: true},
		{name: "nonempty mismatched response key is rejected", wantError: true},
	} {
		t.Run(test.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
			defer cancel()
			listener := newProviderEvidenceTestHost(t, nil)
			provider := newProviderEvidenceTestHost(t, nil)
			querier := newProviderEvidenceTestHost(t, nil)
			key, _, err := providerCIDForIdentity(provider.ID())
			if err != nil {
				t.Fatal(err)
			}
			responseKey := key.Hash()
			if test.emptyKey {
				responseKey = nil
			} else {
				responseKey = []byte("wrong-provider-key")
			}
			handlerResult := make(chan error, 1)
			listener.SetStreamHandler(protocol.ID("/ipfs/kad/1.0.0"), func(stream network.Stream) {
				defer stream.Close()
				if _, err := readFrame(bufio.NewReader(stream)); err != nil {
					handlerResult <- err
					return
				}
				response := kadpb.NewMessage(kadpb.Message_GET_PROVIDERS, responseKey, 0)
				response.ProviderPeers = kadpb.RawPeerInfosToPBPeers([]peer.AddrInfo{{ID: provider.ID(), Addrs: provider.Addrs()}})
				encoded, err := proto.Marshal(response)
				if err == nil {
					err = writeFrame(stream, encoded)
				}
				handlerResult <- err
			})
			listenerInfo := peer.AddrInfo{ID: listener.ID(), Addrs: listener.Addrs()}
			if err := querier.Connect(ctx, listenerInfo); err != nil {
				t.Fatal(err)
			}

			found, _, err := explicitGetProvidersWireEvidenceWithTimeout(
				ctx, querier, listener.ID(), provider.ID(), key, time.Second)
			if test.wantError {
				if err == nil {
					t.Fatal("accepted a nonempty mismatched GET_PROVIDERS response key")
				}
			} else if err != nil {
				t.Fatal(err)
			} else if found.ID != provider.ID() || len(found.Addrs) == 0 {
				t.Fatalf("omitted response key did not return P with addresses: %+v", found)
			}
			if handlerErr := <-handlerResult; handlerErr != nil {
				t.Fatalf("GET_PROVIDERS handler: %v", handlerErr)
			}
		})
	}
}

func TestExplicitGetProvidersWireEvidenceTimesOutStalledResponse(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	listener := newProviderEvidenceTestHost(t, nil)
	provider := newProviderEvidenceTestHost(t, nil)
	querier := newProviderEvidenceTestHost(t, nil)
	key, _, err := providerCIDForIdentity(provider.ID())
	if err != nil {
		t.Fatal(err)
	}
	stalled := make(chan struct{})
	targetFinished := make(chan error, 1)
	var targetOnce sync.Once
	listener.SetStreamHandler(protocol.ID("/ipfs/kad/1.0.0"), func(stream network.Stream) {
		target := false
		var targetErr error
		defer func() {
			if closeErr := stream.Close(); closeErr != nil && !errors.Is(closeErr, network.ErrReset) &&
				!strings.Contains(closeErr.Error(), "stream reset") {
				targetErr = fmt.Errorf("close stalled GET_PROVIDERS handler: %w", closeErr)
			}
			if target {
				targetFinished <- targetErr
			}
		}()

		frame, err := readFrame(bufio.NewReader(stream))
		if err != nil {
			return
		}
		request := &kadpb.Message{}
		if err := proto.Unmarshal(frame, request); err != nil {
			return
		}
		if request.GetType() == kadpb.Message_GET_PROVIDERS && bytes.Equal(request.GetKey(), key.Hash()) {
			targetOnce.Do(func() {
				target = true
				close(stalled)
			})
			if target {
				if _, err := readFrame(bufio.NewReader(stream)); err == nil {
					targetErr = errors.New("stalled GET_PROVIDERS stream accepted a second frame instead of reset")
				} else if !errors.Is(err, network.ErrReset) && !strings.Contains(err.Error(), "stream reset") {
					targetErr = fmt.Errorf("stalled GET_PROVIDERS stream ended without reset: %w", err)
				}
				return
			}
		}

		response := kadpb.NewMessage(request.GetType(), request.GetKey(), request.GetClusterLevel())
		encoded, err := proto.Marshal(response)
		if err == nil {
			_ = writeFrame(stream, encoded)
		}
	})
	listenerInfo := peer.AddrInfo{ID: listener.ID(), Addrs: listener.Addrs()}
	if err := querier.Connect(ctx, listenerInfo); err != nil {
		t.Fatal(err)
	}

	started := time.Now()
	_, _, err = explicitGetProvidersWireEvidenceWithTimeout(
		ctx, querier, listener.ID(), provider.ID(), key, 75*time.Millisecond)
	if err == nil {
		t.Fatal("accepted a stalled GET_PROVIDERS response")
	}
	if !strings.Contains(err.Error(), "read GET_PROVIDERS response") {
		t.Fatalf("stalled GET_PROVIDERS failed outside its read phase: %v", err)
	}
	var timeoutErr net.Error
	if !errors.As(err, &timeoutErr) || !timeoutErr.Timeout() {
		t.Fatalf("stalled GET_PROVIDERS read did not return a typed timeout: %v", err)
	}
	if elapsed := time.Since(started); elapsed >= time.Second {
		t.Fatalf("stalled GET_PROVIDERS read exceeded deadline: %s", elapsed)
	}
	select {
	case <-stalled:
	case <-time.After(time.Second):
		t.Fatal("GET_PROVIDERS handler did not receive the request before timeout")
	}
	select {
	case handlerErr := <-targetFinished:
		if handlerErr != nil {
			t.Fatal(handlerErr)
		}
	case <-time.After(time.Second):
		t.Fatal("stalled GET_PROVIDERS handler did not finish after stream reset")
	}
}

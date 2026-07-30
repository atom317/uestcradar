package server

import (
	"testing"
	"time"

	pb "uestcradar/telemetry/internal/telemetrypb"
)

func metric(nodeID, linkID string, sequence uint64) *pb.RingBufferMetric {
	return &pb.RingBufferMetric{
		NodeId:   nodeID,
		LinkId:   linkID,
		Sequence: sequence,
	}
}

func TestStoreRegistersAndGroupsNodes(t *testing.T) {
	store := NewStore(3)
	seenAt := time.Date(2026, time.July, 30, 12, 0, 0, 0, time.UTC)
	store.Update(metric("node-b", "upstream", 1), seenAt)
	store.Update(metric("node-a", "upstream", 2), seenAt)
	store.Update(metric("node-a", "downstream", 3), seenAt)

	nodes := store.NodesSnapshot()
	if len(nodes) != 2 {
		t.Fatalf("node count = %d, want 2", len(nodes))
	}
	if nodes[0].NodeID != "node-a" || nodes[1].NodeID != "node-b" {
		t.Fatalf("node order = %q, %q", nodes[0].NodeID, nodes[1].NodeID)
	}
	if nodes[0].Status != NodeOnline || !nodes[0].LastSeen.Equal(seenAt) {
		t.Fatalf("node-a lease = %q at %v", nodes[0].Status, nodes[0].LastSeen)
	}
	if len(nodes[0].Links) != 2 ||
		nodes[0].Links[0].LinkID != "downstream" ||
		nodes[0].Links[1].LinkID != "upstream" {
		t.Fatalf("node-a links = %#v", nodes[0].Links)
	}
}

func TestStoreBoundedHistoryAndSnapshotIsolation(t *testing.T) {
	store := NewStore(2)
	seenAt := time.Now()
	for _, sequence := range []uint64{1, 2, 3} {
		store.Update(metric("node", "link", sequence), seenAt)
	}

	first := store.NodesSnapshot()
	points := first[0].Links[0].Points
	if len(points) != 2 || points[0].Sequence != 2 || points[1].Sequence != 3 {
		t.Fatalf("bounded points = %#v", points)
	}
	points[0].Sequence = 99
	if got := store.NodesSnapshot()[0].Links[0].Points[0].Sequence; got != 2 {
		t.Fatalf("snapshot mutated store: sequence = %d", got)
	}
}

func TestStoreLeaseExpiresAndRecovers(t *testing.T) {
	store := NewStore(2)
	startedAt := time.Date(2026, time.July, 30, 12, 0, 0, 0, time.UTC)
	store.Update(metric("node", "upstream", 1), startedAt)

	store.MarkOffline(startedAt.Add(3*time.Second), 3*time.Second)
	if got := store.NodesSnapshot()[0].Status; got != NodeOnline {
		t.Fatalf("status at TTL = %q, want online", got)
	}
	store.MarkOffline(startedAt.Add(3*time.Second+time.Nanosecond), 3*time.Second)
	if got := store.NodesSnapshot()[0].Status; got != NodeOffline {
		t.Fatalf("status after TTL = %q, want offline", got)
	}

	recoveredAt := startedAt.Add(4 * time.Second)
	store.Update(metric("node", "upstream", 2), recoveredAt)
	node := store.NodesSnapshot()[0]
	if node.Status != NodeOnline || !node.LastSeen.Equal(recoveredAt) {
		t.Fatalf("recovered lease = %q at %v", node.Status, node.LastSeen)
	}
}

func TestStoreRejectsMalformedIdentity(t *testing.T) {
	store := NewStore(2)
	store.Update(nil, time.Now())
	store.Update(metric("", "upstream", 1), time.Now())
	store.Update(metric("node", "", 1), time.Now())
	if got := store.NodesSnapshot(); len(got) != 0 {
		t.Fatalf("registered malformed nodes: %#v", got)
	}
}

func TestMetricsSnapshotRemainsFlattened(t *testing.T) {
	store := NewStore(2)
	seenAt := time.Now()
	store.Update(metric("node-b", "upstream", 1), seenAt)
	store.Update(metric("node-a", "upstream", 2), seenAt)
	store.Update(metric("node-a", "downstream", 3), seenAt)

	series := store.MetricsSnapshot()
	if len(series) != 3 {
		t.Fatalf("series count = %d, want 3", len(series))
	}
	got := []string{
		series[0].NodeID + "/" + series[0].LinkID,
		series[1].NodeID + "/" + series[1].LinkID,
		series[2].NodeID + "/" + series[2].LinkID,
	}
	want := []string{"node-a/downstream", "node-a/upstream", "node-b/upstream"}
	for index := range want {
		if got[index] != want[index] {
			t.Errorf("series[%d] = %q, want %q", index, got[index], want[index])
		}
	}
}

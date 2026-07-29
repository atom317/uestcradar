package server

import (
	"sort"
	"sync"
	"time"

	pb "uestcradar/telemetry/internal/telemetrypb"
)

// NodeStatus is the lease state of a registered Sidecar node.
type NodeStatus string

const (
	NodeOnline  NodeStatus = "online"
	NodeOffline NodeStatus = "offline"
)

// Point is the JSON representation retained for one ring sample.
type Point struct {
	ObservedUnixNS uint64 `json:"observed_unix_ns"`
	CapacityBytes  uint64 `json:"capacity_bytes"`
	UsedBytes      uint64 `json:"used_bytes"`
	WritePosition  uint64 `json:"write_position"`
	ReadPosition   uint64 `json:"read_position"`
	Sequence       uint64 `json:"sequence"`
	Shutdown       bool   `json:"shutdown"`
}

// Series is the backwards-compatible flattened metric representation.
type Series struct {
	NodeID string  `json:"node_id"`
	LinkID string  `json:"link_id"`
	Points []Point `json:"points"`
}

// LinkSnapshot contains one link's bounded history inside a node.
type LinkSnapshot struct {
	LinkID string  `json:"link_id"`
	Points []Point `json:"points"`
}

// NodeSnapshot is the node-centric representation consumed by the dashboard.
type NodeSnapshot struct {
	NodeID   string         `json:"node_id"`
	Status   NodeStatus     `json:"status"`
	LastSeen time.Time      `json:"last_seen"`
	Links    []LinkSnapshot `json:"links"`
}

type nodeRecord struct {
	lastSeen time.Time
	status   NodeStatus
	links    map[string]Series
}

// Store retains a process-lifetime node registry and bounded link histories.
type Store struct {
	mu    sync.RWMutex
	limit int
	nodes map[string]*nodeRecord
}

// NewStore creates an empty registry.
func NewStore(limit int) *Store {
	if limit < 1 {
		limit = 1
	}
	return &Store{
		limit: limit,
		nodes: make(map[string]*nodeRecord),
	}
}

// Update registers or refreshes a node and appends one link sample.
func (s *Store) Update(metric *pb.RingBufferMetric, receivedAt time.Time) {
	if metric == nil || metric.NodeId == "" || metric.LinkId == "" {
		return
	}

	point := Point{
		ObservedUnixNS: metric.ObservedUnixNs,
		CapacityBytes:  metric.CapacityBytes,
		UsedBytes:      metric.UsedBytes,
		WritePosition:  metric.WritePosition,
		ReadPosition:   metric.ReadPosition,
		Sequence:       metric.Sequence,
		Shutdown:       metric.Shutdown,
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	node := s.nodes[metric.NodeId]
	if node == nil {
		node = &nodeRecord{
			status: NodeOnline,
			links:  make(map[string]Series),
		}
		s.nodes[metric.NodeId] = node
	}
	node.lastSeen = receivedAt
	node.status = NodeOnline

	series := node.links[metric.LinkId]
	series.NodeID = metric.NodeId
	series.LinkID = metric.LinkId
	if len(series.Points) == s.limit {
		copy(series.Points, series.Points[1:])
		series.Points[len(series.Points)-1] = point
	} else {
		series.Points = append(series.Points, point)
	}
	node.links[metric.LinkId] = series
}

// MarkOffline expires node leases without deleting their last known metrics.
func (s *Store) MarkOffline(now time.Time, ttl time.Duration) {
	s.mu.Lock()
	defer s.mu.Unlock()

	for _, node := range s.nodes {
		if now.Sub(node.lastSeen) > ttl {
			node.status = NodeOffline
		}
	}
}

// NodesSnapshot returns an isolated, deterministically ordered registry view.
func (s *Store) NodesSnapshot() []NodeSnapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]NodeSnapshot, 0, len(s.nodes))
	for nodeID, node := range s.nodes {
		links := make([]LinkSnapshot, 0, len(node.links))
		for _, series := range node.links {
			links = append(links, LinkSnapshot{
				LinkID: series.LinkID,
				Points: append([]Point(nil), series.Points...),
			})
		}
		sort.Slice(links, func(i, j int) bool {
			return links[i].LinkID < links[j].LinkID
		})
		result = append(result, NodeSnapshot{
			NodeID:   nodeID,
			Status:   node.status,
			LastSeen: node.lastSeen,
			Links:    links,
		})
	}
	sort.Slice(result, func(i, j int) bool {
		return result[i].NodeID < result[j].NodeID
	})
	return result
}

// MetricsSnapshot preserves the original flattened /api/metrics contract.
func (s *Store) MetricsSnapshot() []Series {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]Series, 0)
	for _, node := range s.nodes {
		for _, series := range node.links {
			series.Points = append([]Point(nil), series.Points...)
			result = append(result, series)
		}
	}
	sort.Slice(result, func(i, j int) bool {
		if result[i].NodeID == result[j].NodeID {
			return result[i].LinkID < result[j].LinkID
		}
		return result[i].NodeID < result[j].NodeID
	})
	return result
}

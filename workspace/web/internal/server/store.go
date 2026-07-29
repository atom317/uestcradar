package server

import (
	"sort"
	"sync"

	pb "uestcradar/telemetry/internal/telemetrypb"
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

// Series contains recent samples for one node and link.
type Series struct {
	NodeID string  `json:"node_id"`
	LinkID string  `json:"link_id"`
	Points []Point `json:"points"`
}

// Store retains a bounded in-memory history for every observed link.
type Store struct {
	mu      sync.RWMutex
	limit   int
	history map[string]Series
}

// NewStore creates a bounded telemetry history.
func NewStore(limit int) *Store {
	return &Store{limit: limit, history: make(map[string]Series)}
}

// Update appends one protobuf metric, dropping the oldest point when full.
func (s *Store) Update(metric *pb.RingBufferMetric) {
	key := metric.NodeId + "\x00" + metric.LinkId
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

	series := s.history[key]
	series.NodeID = metric.NodeId
	series.LinkID = metric.LinkId
	if len(series.Points) == s.limit {
		copy(series.Points, series.Points[1:])
		series.Points[len(series.Points)-1] = point
	} else {
		series.Points = append(series.Points, point)
	}
	s.history[key] = series
}

// Snapshot returns an isolated, deterministically ordered copy.
func (s *Store) Snapshot() []Series {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]Series, 0, len(s.history))
	for _, item := range s.history {
		item.Points = append([]Point(nil), item.Points...)
		result = append(result, item)
	}
	sort.Slice(result, func(i, j int) bool {
		if result[i].NodeID == result[j].NodeID {
			return result[i].LinkID < result[j].LinkID
		}
		return result[i].NodeID < result[j].NodeID
	})
	return result
}

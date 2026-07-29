package server

import (
	"testing"

	pb "uestcradar/telemetry/internal/telemetrypb"
)

func TestStoreBoundedHistory(t *testing.T) {
	tests := []struct {
		name      string
		limit     int
		sequences []uint64
		want      []uint64
	}{
		{name: "under limit", limit: 3, sequences: []uint64{1, 2}, want: []uint64{1, 2}},
		{name: "drops oldest", limit: 2, sequences: []uint64{1, 2, 3}, want: []uint64{2, 3}},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			store := NewStore(test.limit)
			for _, sequence := range test.sequences {
				store.Update(&pb.RingBufferMetric{
					NodeId: "node", LinkId: "link", Sequence: sequence,
				})
			}

			got := store.Snapshot()[0].Points
			if len(got) != len(test.want) {
				t.Fatalf("point count = %d, want %d", len(got), len(test.want))
			}
			for index, point := range got {
				if point.Sequence != test.want[index] {
					t.Errorf(
						"sequence[%d] = %d, want %d",
						index,
						point.Sequence,
						test.want[index],
					)
				}
			}
		})
	}
}

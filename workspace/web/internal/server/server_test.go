package server

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestNodesAndMetricsEndpoints(t *testing.T) {
	store := NewStore(2)
	seenAt := time.Date(2026, time.July, 30, 12, 0, 0, 0, time.UTC)
	store.Update(metric("node-a", "upstream", 7), seenAt)
	handler := newHTTPHandler(store)

	nodesResponse := httptest.NewRecorder()
	handler.ServeHTTP(
		nodesResponse,
		httptest.NewRequest(http.MethodGet, "/api/nodes", nil),
	)
	if nodesResponse.Code != http.StatusOK {
		t.Fatalf("/api/nodes status = %d", nodesResponse.Code)
	}
	var nodes []NodeSnapshot
	if err := json.NewDecoder(nodesResponse.Body).Decode(&nodes); err != nil {
		t.Fatalf("decode /api/nodes: %v", err)
	}
	if len(nodes) != 1 || nodes[0].NodeID != "node-a" ||
		nodes[0].Status != NodeOnline || len(nodes[0].Links) != 1 {
		t.Fatalf("/api/nodes body = %#v", nodes)
	}

	metricsResponse := httptest.NewRecorder()
	handler.ServeHTTP(
		metricsResponse,
		httptest.NewRequest(http.MethodGet, "/api/metrics", nil),
	)
	if metricsResponse.Code != http.StatusOK {
		t.Fatalf("/api/metrics status = %d", metricsResponse.Code)
	}
	var series []Series
	if err := json.NewDecoder(metricsResponse.Body).Decode(&series); err != nil {
		t.Fatalf("decode /api/metrics: %v", err)
	}
	if len(series) != 1 || series[0].NodeID != "node-a" ||
		series[0].LinkID != "upstream" {
		t.Fatalf("/api/metrics body = %#v", series)
	}
}

#!/bin/bash
echo "=== Create Group ==="
curl -s -X POST http://localhost:8000/api/groups \
  -H "Content-Type: application/json" \
  -d '{"org_id":1,"name":"Test Group"}'
echo ""

echo "=== List Groups ==="
curl -s http://localhost:8000/api/groups
echo ""

echo "=== Create Service ==="
curl -s -X POST http://localhost:8000/api/services \
  -H "Content-Type: application/json" \
  -d '{"group_id":1,"tsp_code":100,"name":"Test Service","price":500}'
echo ""

echo "=== List Services ==="
curl -s http://localhost:8000/api/services?group_id=1
echo ""

echo "=== Stats ==="
curl -s http://localhost:8000/api/stats
echo ""

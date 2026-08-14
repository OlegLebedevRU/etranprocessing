#!/bin/bash
echo "=== Try delete group with service (should fail) ==="
curl -s -X DELETE http://localhost:8000/api/groups/1
echo ""

echo "=== Delete service ==="
curl -s -X DELETE http://localhost:8000/api/services/1
echo ""

echo "=== Delete group ==="
curl -s -X DELETE http://localhost:8000/api/groups/1
echo ""

echo "=== Final stats ==="
curl -s http://localhost:8000/api/stats
echo ""

from app.services import portal_changes as bus


def test_device_and_file_keys_cannot_cross_tenants_or_id_namespaces():
    with (
        bus.subscribe(7, 12, 32) as owner,
        bus.subscribe(9, 12, 32) as other,
        bus.subscribe(7, 32) as other_device,
        bus.subscribe(7, None) as inventory,
    ):
        bus.dispatch({"v": 1, "org": 7, "terminal": 32, "kinds": ["files"]})
        assert owner.pending == {"files"}
        assert not other.pending and not other_device.pending and not inventory.pending
        bus.dispatch({"v": 1, "org": 7, "device": 12, "kinds": ["quality", "presence"]})
        assert owner.pending == {"files", "quality", "presence"}
        assert inventory.pending == {"presence"}
        assert not other.pending and not other_device.pending


def test_invalid_events_and_duplicates_do_not_grow_subscriber_queue():
    with bus.subscribe(3, 5) as watch:
        for payload in (
            {"org": 3, "device": 5, "kinds": [{}]},
            {"org": True, "device": 5, "kinds": ["files"]},
            {"org": 3, "device": 5, "terminal": 8, "kinds": ["quality"]},
        ):
            bus.dispatch({"v": 1, **payload})
        assert not watch.pending
        for _ in range(20):
            bus.dispatch({"v": 1, "org": 3, "device": 5, "kinds": ["quality"]})
        assert watch.pending == {"quality"}

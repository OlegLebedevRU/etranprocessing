from types import SimpleNamespace

from app.services import file_change_publisher as publisher


def test_rolled_back_and_nested_file_changes_do_not_publish(monkeypatch):
    monkeypatch.setattr(publisher, "_pending", set())
    s = SimpleNamespace(
        info={publisher._KEY: {(1, 2)}}, in_nested_transaction=lambda: True
    )
    publisher.committed(s)
    assert not publisher._pending
    publisher.discard(s)
    s.in_nested_transaction = lambda: False
    publisher.committed(s)
    assert not publisher._pending
    s.info[publisher._KEY] = {(3, 4)}
    publisher.committed(s)
    assert publisher._pending == {(3, 4)}

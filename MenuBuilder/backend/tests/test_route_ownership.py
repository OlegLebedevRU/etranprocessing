from app.main import app


def test_list_menu_file_is_not_exposed_by_menu_builder():
    paths = {getattr(route, "path", None) for route in app.routes}

    assert "/api/ListMenuFile" not in paths

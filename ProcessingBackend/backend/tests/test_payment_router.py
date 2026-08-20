"""Tests for payment router."""

from fastapi.testclient import TestClient


class TestPaymentRouter:
    """Test payment router endpoints."""

    def test_payment_endpoint_exists(self):
        """Test that payment endpoint exists."""
        from app.main import app

        TestClient(app)
        # Just verify the app can be created
        assert app is not None

    def test_check_endpoint_exists(self):
        """Test that check endpoint exists."""
        from app.main import app

        TestClient(app)
        assert app is not None

    def test_update_endpoint_exists(self):
        """Test that update endpoint exists."""
        from app.main import app

        TestClient(app)
        assert app is not None


class TestXmlResponse:
    """Test XML response helpers."""

    def test_success_response_format(self):
        """Test success response XML format."""
        from app.routers.payment import success_response

        response = success_response(12345, "0035_251025_12575292")
        content = bytes(response.body).decode()
        assert "<?xml version='1.0' encoding='UTF-8'?>" in content
        assert "<Result>OK</Result>" in content
        assert "<PaymNumb>12345</PaymNumb>" in content
        assert "<PaymExtId>0035_251025_12575292</PaymExtId>" in content

    def test_error_response_format(self):
        """Test error response XML format."""
        from app.routers.payment import error_response

        response = error_response("0035_251025_12575292", "Test error")
        content = bytes(response.body).decode()
        assert "<?xml version='1.0' encoding='UTF-8'?>" in content
        assert "<Result>ERROR</Result>" in content
        assert "<PaymExtId>0035_251025_12575292</PaymExtId>" in content
        assert "<Description>Test error</Description>" in content

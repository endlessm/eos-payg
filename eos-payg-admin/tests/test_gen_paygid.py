#!/usr/bin/env python3

import json
import unittest
from io import BytesIO
from unittest.mock import MagicMock, patch, call

from gen_paygid import GenPAYGUnits


def make_mock_response(status, body):
    """Helper to build a mock HTTPResponse."""
    mock_res = MagicMock()
    mock_res.status = status
    mock_res.read.return_value = json.dumps(body).encode("utf-8")
    return mock_res


class TestLogin(unittest.TestCase):

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_login_success_stores_access_token(self, mock_https):
        mock_conn = MagicMock()
        mock_https.return_value = mock_conn
        mock_conn.getresponse.return_value = make_mock_response(
            200, {"access_token": "tok_abc123"}
        )

        obj = GenPAYGUnits("client1", "secret1", "orgtoken1")

        self.assertEqual(obj.access_token, "tok_abc123")

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_login_sends_correct_payload_and_headers(self, mock_https):
        mock_conn = MagicMock()
        mock_https.return_value = mock_conn
        mock_conn.getresponse.return_value = make_mock_response(
            200, {"access_token": "tok_xyz"}
        )

        GenPAYGUnits("my_client", "my_secret", "my_org_token")

        mock_https.assert_called_once_with("api.angaza.com")
        args, kwargs = mock_conn.request.call_args
        method, path, payload, headers = args
        self.assertEqual(method, "POST")
        self.assertEqual(path, "/identity/resources/auth/v2/api-token")
        body = json.loads(payload)
        self.assertEqual(body["clientId"], "my_client")
        self.assertEqual(body["secret"], "my_secret")
        self.assertEqual(headers["X-Api-Key"], "my_org_token")
        self.assertEqual(headers["Content-Type"], "application/json")

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_login_missing_access_token_raises_key_error(self, mock_https):
        mock_conn = MagicMock()
        mock_https.return_value = mock_conn
        mock_conn.getresponse.return_value = make_mock_response(200, {})

        with self.assertRaises(KeyError):
            GenPAYGUnits("c", "s", "o")


class TestGenerateUnits(unittest.TestCase):

    def _make_obj(self, mock_https, access_token="tok_test"):
        """Instantiate GenPAYGUnits with a mocked Login response."""
        mock_conn = MagicMock()
        mock_https.return_value = mock_conn
        mock_conn.getresponse.return_value = make_mock_response(
            200, {"access_token": access_token}
        )
        obj = GenPAYGUnits("c", "s", "o")
        # Reset mock so Login calls are not counted in later assertions
        mock_conn.reset_mock()
        return obj, mock_conn

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_generate_completed_immediately_returns_result_url(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(200, {
            "completed_when": "2024-01-01T00:00:00Z",
            "_links": {"result": {"href": "/nexus/v2/units/batches/42/result"}},
        })

        url, completed = obj.Generate_Units(count=5)

        self.assertTrue(completed)
        self.assertEqual(url, "/nexus/v2/units/batches/42/result")

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_generate_not_completed_returns_self_url(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(200, {
            "completed_when": None,
            "_links": {"self": {"href": "/nexus/v2/units/batches/42"}},
        })

        url, completed = obj.Generate_Units(count=10)

        self.assertFalse(completed)
        self.assertEqual(url, "/nexus/v2/units/batches/42")

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_generate_sends_correct_payload_and_auth_header(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https, access_token="bearer_tok")
        mock_conn.getresponse.return_value = make_mock_response(200, {
            "completed_when": "2024-01-01T00:00:00Z",
            "_links": {"result": {"href": "/result"}},
        })

        obj.Generate_Units(count=3)

        args, kwargs = mock_conn.request.call_args
        method, path, payload, headers = (args[0], args[1], args[2], kwargs.get("headers", args[3] if len(args) > 3 else {}))
        # kwargs may store headers
        if not headers:
            headers = kwargs.get("headers", {})
        self.assertEqual(method, "POST")
        self.assertEqual(path, "/nexus/v2/units")
        body = json.loads(payload)
        self.assertEqual(body["product_qid"], "PT000421")
        self.assertEqual(body["count"], 3)
        self.assertIn("Bearer bearer_tok", headers.get("Authorization", ""))

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_generate_http_error_raises_exception(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(401, {"error": "Unauthorized"})

        with self.assertRaises(Exception) as ctx:
            obj.Generate_Units()

        self.assertIn("401", str(ctx.exception))

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_generate_500_error_raises_exception(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(500, {"error": "Server Error"})

        with self.assertRaises(Exception) as ctx:
            obj.Generate_Units()

        self.assertIn("500", str(ctx.exception))

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_generate_default_count_is_one(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(200, {
            "completed_when": "2024-01-01T00:00:00Z",
            "_links": {"result": {"href": "/result"}},
        })

        obj.Generate_Units()

        args, _ = mock_conn.request.call_args
        body = json.loads(args[2])
        self.assertEqual(body["count"], 1)


class TestWaitComplete(unittest.TestCase):

    def _make_obj(self, mock_https, access_token="tok_test"):
        mock_conn = MagicMock()
        mock_https.return_value = mock_conn
        mock_conn.getresponse.return_value = make_mock_response(
            200, {"access_token": access_token}
        )
        obj = GenPAYGUnits("c", "s", "o")
        mock_conn.reset_mock()
        return obj, mock_conn

    @patch("gen_paygid.time.sleep", return_value=None)
    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_wait_polls_until_completed(self, mock_https, mock_sleep):
        obj, mock_conn = self._make_obj(mock_https)

        # First call: not done; second call: done
        mock_conn.getresponse.side_effect = [
            make_mock_response(200, {
                "completed_when": None,
                "_links": {"result": {"href": "/result"}},
            }),
            make_mock_response(200, {
                "completed_when": "2024-01-01T00:00:00Z",
                "_links": {"result": {"href": "/nexus/v2/units/batches/42/result"}},
            }),
        ]

        result_url = obj.Wait_Complete("/nexus/v2/units/batches/42")

        self.assertEqual(result_url, "/nexus/v2/units/batches/42/result")
        self.assertEqual(mock_sleep.call_count, 2)

    @patch("gen_paygid.time.sleep", return_value=None)
    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_wait_returns_result_href_on_first_poll_if_ready(self, mock_https, mock_sleep):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(200, {
            "completed_when": "2024-01-01T00:00:00Z",
            "_links": {"result": {"href": "/result/url"}},
        })

        result_url = obj.Wait_Complete("/nexus/v2/units/batches/99")

        self.assertEqual(result_url, "/result/url")
        mock_sleep.assert_called_once_with(10)

    @patch("gen_paygid.time.sleep", return_value=None)
    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_wait_http_error_raises_exception(self, mock_https, mock_sleep):
        obj, mock_conn = self._make_obj(mock_https)
        mock_conn.getresponse.return_value = make_mock_response(403, {"error": "Forbidden"})

        with self.assertRaises(Exception) as ctx:
            obj.Wait_Complete("/nexus/v2/units/batches/99")

        self.assertIn("403", str(ctx.exception))

    @patch("gen_paygid.time.sleep", return_value=None)
    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_wait_sends_get_request_with_auth(self, mock_https, mock_sleep):
        obj, mock_conn = self._make_obj(mock_https, access_token="my_token")
        mock_conn.getresponse.return_value = make_mock_response(200, {
            "completed_when": "2024-01-01T00:00:00Z",
            "_links": {"result": {"href": "/result"}},
        })

        obj.Wait_Complete("/nexus/v2/units/batches/7")

        args, kwargs = mock_conn.request.call_args
        method, path = args[0], args[1]
        headers = kwargs.get("headers", args[2] if len(args) > 2 else {})
        self.assertEqual(method, "GET")
        self.assertEqual(path, "/nexus/v2/units/batches/7")
        self.assertIn("Bearer my_token", headers.get("Authorization", ""))


class TestGetCSV(unittest.TestCase):

    def _make_obj(self, mock_https, access_token="tok_test"):
        mock_conn = MagicMock()
        mock_https.return_value = mock_conn
        mock_conn.getresponse.return_value = make_mock_response(
            200, {"access_token": access_token}
        )
        obj = GenPAYGUnits("c", "s", "o")
        mock_conn.reset_mock()
        return obj, mock_conn

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_get_csv_redirect_prints_download_url(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        mock_res = MagicMock()
        mock_res.status = 302
        mock_res.getheader.return_value = "https://s3.example.com/payg-ids.csv"
        mock_conn.getresponse.return_value = mock_res

        with patch("builtins.print") as mock_print:
            obj.Get_CSV("/nexus/v2/units/batches/42/result")

        mock_print.assert_called_once()
        printed = mock_print.call_args[0][0]
        self.assertIn("https://s3.example.com/payg-ids.csv", printed)

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_get_csv_no_redirect_prints_json_data(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https)
        body = {"units": [{"paygid": "PG001"}, {"paygid": "PG002"}]}
        mock_conn.getresponse.return_value = make_mock_response(200, body)

        with patch("builtins.print") as mock_print:
            obj.Get_CSV("/nexus/v2/units/batches/42/result")

        mock_print.assert_called_once_with(body)

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_get_csv_sends_get_with_auth_headers(self, mock_https):
        obj, mock_conn = self._make_obj(mock_https, access_token="csv_token")
        mock_conn.getresponse.return_value = make_mock_response(200, {})

        with patch("builtins.print"):
            obj.Get_CSV("/nexus/v2/units/batches/1/result")

        args, kwargs = mock_conn.request.call_args
        method, path = args[0], args[1]
        headers = kwargs.get("headers", args[2] if len(args) > 2 else {})
        self.assertEqual(method, "GET")
        self.assertEqual(path, "/nexus/v2/units/batches/1/result")
        self.assertIn("Bearer csv_token", headers.get("Authorization", ""))

    @patch("gen_paygid.http.client.HTTPSConnection")
    def test_get_csv_all_redirect_codes_handled(self, mock_https):
        for status_code in (301, 302, 303, 307, 308):
            with self.subTest(status=status_code):
                obj, mock_conn = self._make_obj(mock_https)
                mock_res = MagicMock()
                mock_res.status = status_code
                mock_res.getheader.return_value = "https://cdn.example.com/file.csv"
                mock_conn.getresponse.return_value = mock_res

                with patch("builtins.print") as mock_print:
                    obj.Get_CSV("/result")

                mock_print.assert_called_once()
                self.assertIn(
                    "https://cdn.example.com/file.csv",
                    mock_print.call_args[0][0],
                )


if __name__ == "__main__":
    unittest.main()

import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

class OSMPMockHandler(BaseHTTPRequestHandler):
    def _handle(self):
        parsed = urlparse(self.path)
        if parsed.path.rstrip('/') != '/osmp':
            self.send_response(404)
            self.end_headers()
            return

        query_params = parse_qs(parsed.query)
        command = query_params.get('command', [''])[0]
        account = query_params.get('account', [''])[0]
        txn_id = query_params.get('txn_id', ['0'])[0]

        # Read POST body if present
        content_length = int(self.headers.get('Content-Length', 0))
        if content_length > 0:
            post_body = self.rfile.read(content_length).decode('utf-8', errors='ignore')
            post_params = parse_qs(post_body)
            if not command:
                command = post_params.get('command', [''])[0]
            if not account:
                account = post_params.get('account', [''])[0]
            if txn_id == '0':
                txn_id = post_params.get('txn_id', ['0'])[0]

        if command != 'check':
            response_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<response>
    <osmp_txn_id>{txn_id}</osmp_txn_id>
    <result>3</result>
    <comment>Команда '{command}' не поддерживается</comment>
</response>
"""
        elif account == '5555':
            res_sum = "120.00"
            res_comment = "OK"
            recipient = "Лицевой счет: 5555, Сумма к оплате: 120.00 руб."
            response_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<response>
    <osmp_txn_id>{txn_id}</osmp_txn_id>
    <result>0</result>
    <comment>{res_comment}</comment>
    <sum>{res_sum}</sum>
    <balance>{res_sum}</balance>
    <recipient_name>{recipient}</recipient_name>
    <fields>
        <field1>Счет: 5555</field1>
        <field2>К оплате: 120.00 руб.</field2>
    </fields>
</response>
"""
        else:
            response_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<response>
    <osmp_txn_id>{txn_id}</osmp_txn_id>
    <result>5</result>
    <comment>Счет не найден</comment>
</response>
"""

        data = response_xml.strip().encode('utf-8')
        self.send_response(200)
        self.send_header('Content-Type', 'text/xml; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._handle()

    def do_POST(self):
        self._handle()

def run():
    server = HTTPServer(('127.0.0.1', 8008), OSMPMockHandler)
    print("OSMP mock server listening on http://127.0.0.1:8008/osmp", flush=True)
    server.serve_forever()

if __name__ == '__main__':
    run()

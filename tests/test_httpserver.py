import requests
import socket
import time
base_url = 'http://127.0.0.1:8080'

def test_simple_get():
    print("\n-----Starting GET test-----")
    try:
        r = requests.get(f"{base_url}/log")
        print(f"Status code: {r.status_code}")
        print(f"Response Body: {r.text}")
        assert r.status_code == 200
        print("✅ PASSED: GET test")
    except Exception as e:
        print(f"❌ FAILED: GET test\n{e}")

def test_simple_post():
    print("\n-----Starting POST test-----")
    try:
        log_data={"name": "test",
               "language": "python",
               "message": "test post message!"}
        r = requests.post(f"{base_url}/log",json=log_data)
        print(f"Status code: {r.status_code}")
        print(f"Response Body: {r.text}")
        assert r.status_code == 200
        print("✅ PASSED: POST test")
    except Exception as e:
        print(f"❌ FAILED: POST test\n{e}")
def test_404_error():
    print("\n-----Starting 404 error test-----")
    try:
        r = requests.get(f"{base_url}/not_exist_path")
        print(f"Status code: {r.status_code}")
        print(f"Response Body: {r.text}")
        assert r.status_code == 404
        print("✅ PASSED: 404 error test")
    except Exception as e:
        print(f"❌ FAILED: 404 error test\n{e}")
def test_keep_alive():
    print("\n-----Starting keep-alive test-----")
    try:
        with requests.Session() as s:
            print("Sending first request...")
            resp1 = s.get(f"{base_url}/log")
            assert resp1.status_code == 200
            
            print("Sending second request on the same connection...")
            resp2 = s.get(f"{base_url}/log")
            assert resp2.status_code == 200
        
        print("✅ PASSED: keep-alive test")
    except Exception as e:
        print(f"❌ FAILED: keep-alive test\n{e}")

def test_malformed_request_line():
    print("\n-----Starting malformed request line test-----")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 8080))
            s.sendall(b'GET /log \r\nHost: 127.0.0.1\r\n\r\n')
            data = s.recv(1024)
            data_str = data.decode('utf-8')
            print(f"Received data: {data_str}")
            assert 'HTTP/1.1 400 Bad Request' in data_str
            more_data=s.recv(1024)
            assert not more_data
            print("✅ PASSED: malformed request line test")
    except Exception as e:
        print(f"❌ FAILED: malformed request line test\n{e}")
def test_large_body():
    print("\n-----Starting large body test-----")
    try:
        log_data='a'*1024*1024
        r = requests.post(f"{base_url}/log",data=log_data,headers={"Content-Type": "application/plain"})
        print(f"Status code: {r.status_code}")
        print(f"Response Body: {r.text}")
        assert r.status_code == 200
        print("✅ PASSED: large body test(1MB)")
    except Exception as e:
        print(f"❌ FAILED: large body test(1MB)\n{e}")
def test_slow_client():
    print("\n-----Starting slow client test-----")
    incomplete_request = b'POST /log HTTP/1.1\r\nHost: 127.0.0.1\r\n'
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(('127.0.0.1', 8080))
            s.sendall(incomplete_request)
            print("Sent incomplete request...sleeping for 8 seconds to simulate slow client...")
            time.sleep(8)
            try:
                s.settimeout(1)
                data=s.recv(1024)
                if not data:
                    print("✅ PASSED: slow client test")
                else:
                    print("❌ FAILED: slow client test")
                    print(f"Received data: {data.decode('utf-8')}")
            except socket.timeout:
                print("❌ FAILED: Server kept the connection open for 8 seconds without timing out.")
                print("No response from server within timeout")
    except Exception as e:
        print(f"❌ FAILED: slow client test\n{e}")

if __name__ == '__main__':
    test_simple_get()
    test_simple_post()
    test_404_error()
    test_keep_alive()
    test_malformed_request_line()
    test_large_body()
    test_slow_client()
    print("\n -----all tests complete-----")
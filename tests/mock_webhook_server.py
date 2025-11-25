from fastapi import FastAPI, Request
import uvicorn
import json

app = FastAPI()

@app.post("/webhook")
async def receive_webhook(request: Request):
    try:
        body = await request.json()
        print("\n" + "="*40)
        print("🔔 [MOCK SERVER] 收到报警推送！")
        print("="*40)
        print(json.dumps(body, indent=2, ensure_ascii=False))
        print("="*40 + "\n")
        return {"errcode": 0, "errmsg": "ok"}
    except Exception as e:
        print(f"Error: {e}")
        return {"error": str(e)}

if __name__ == "__main__":
    # 监听 9999 端口
    uvicorn.run(app, host="127.0.0.1", port=9999)
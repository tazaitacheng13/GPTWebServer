import requests
import json

API_KEY="7CeIPUR0rr22ZUPS0H3oSMgG"
SECRET_KEY="igN0Dtp4TCIJgFQAxi1ncSAeTPLlGd2t"

def main():
    url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/eb-instant?access_token=" + get_access_token();

    with open('/home/Cplus/webserver-master/content.txt', 'r') as file:
        content = file.read()

    payload = json.dumps({
        "messages": [
            {
                "role": "user",
                "content": content
            }
        ]
    })

    headers = {
        'Content-Type': 'application/json',
    }

    response = requests.request("POST", url, headers=headers, data=payload)
    data = json.loads(response.text)
    result = data.get("result")
    print(result)

    with open('/home/Cplus/webserver-master/response.txt', 'w') as file:
            file.write(result)



def get_access_token():
    """
    使用 AK，SK 生成鉴权签名（Access Token）
    :return: access_token，或是None(如果错误)
    """
    url = "https://aip.baidubce.com/oauth/2.0/token"
    params = {"grant_type": "client_credentials", "client_id": API_KEY, "client_secret": SECRET_KEY}
    return str(requests.post(url, params=params).json().get("access_token"))


if __name__ == '__main__':
    main()
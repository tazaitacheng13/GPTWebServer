from zhipuai import ZhipuAI
from urllib.parse import unquote
import os

# 设置环境变量
os.environ['PYDANTIC_SKIP_VALIDATING_CORE_SCHEMAS'] = '1'

try:
    print("start create model")
    client = ZhipuAI(api_key="9c0564994f92d32ff71e914f9ff5e593.H6s3738EWDFviLDs")
    print("create client success")
    with open('/home/Cplus/webserver-master/content.txt', 'r') as file:
        content = file.read()

    content = unquote(content)
    response = client.chat.completions.create(
        model="glm-4-air",  # 填写需要调用的模型名称
        messages=[
            {"role": "user", "content": content},
        ],
    )
    print(response.choices[0].message.content)
    with open('/home/Cplus/webserver-master/response.txt', 'w') as file:
        file.write(response.choices[0].message.content)

except Exception as e:
    print(f"An error occurred: {e}")
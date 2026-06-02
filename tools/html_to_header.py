#!/usr/bin/env python3
import os

# Get the directory of the current script
script_dir = os.path.dirname(os.path.abspath(__file__))
# Project root is one level up
project_root = os.path.dirname(script_dir)

html_path = os.path.join(project_root, "assets", "transfer_client.html")
header_path = os.path.join(project_root, "include", "transfer_client_html.h")

def convert():
    if not os.path.exists(html_path):
        print(f"Error: {html_path} does not exist.")
        return

    with open(html_path, 'r', encoding='utf-8') as f:
        html_content = f.read()

    header_content = f"""#pragma once

const char* TRANSFER_CLIENT_HTML = R"rawliteral(
{html_content}
)rawliteral";
"""

    with open(header_path, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"Successfully converted {html_path} to {header_path}")

if __name__ == "__main__":
    convert()

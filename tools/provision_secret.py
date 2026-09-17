"""Provision an identified Agent over USB; input is visible locally unless --hidden is selected."""
import argparse
import getpass
import json
import os
import secrets
import time
from serial_link import connect, receive_until


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--environment-only", action="store_true")
    parser.add_argument("--hidden", action="store_true", help="Hide password and Key input (default: visible locally)")
    parser.add_argument("--gateway-url")
    parser.add_argument("--gateway-ca", help="Public CA certificate PEM file")
    parser.add_argument("--user-id", default="local-user")
    parser.add_argument("--session-id", default="default")
    args = parser.parse_args()
    config = {}
    fields = [("ssid", "WIFI_SSID", False), ("password", "WIFI_PASSWORD", True),
              ("deepseek_api_key", "DEEPSEEK_API_KEY", True)]
    for field, variable, hidden in fields:
        value = os.environ.get(variable)
        if value is None and not args.environment_only:
            prompt = f"{variable} (blank keeps existing): "
            value = getpass.getpass(prompt) if hidden and args.hidden else input(prompt)
        if value:
            config[field] = value
    if args.gateway_url:
        config["gateway_url"] = args.gateway_url
        token = os.environ.get("AGENT_GATEWAY_TOKEN")
        if not token and not args.environment_only:
            token = getpass.getpass("AGENT_GATEWAY_TOKEN: ") if args.hidden else input("AGENT_GATEWAY_TOKEN: ")
        if token:
            config["gateway_token"] = token
    if args.gateway_ca:
        with open(args.gateway_ca, encoding="ascii") as handle:
            config["gateway_ca"] = handle.read()
    config.update(user_id=args.user_id, session_id=args.session_id,
                  epoch=int(time.time()), nonce=secrets.token_hex(8))
    wire = ("@provision " + json.dumps(config, separators=(",", ":")) + "\n").encode()
    if len(wire) > 6144:
        raise ValueError("Provisioning frame exceeds 6 KiB")
    with connect(args.port) as link:
        link.reset_input_buffer()
        link.write(b"agent status\n")
        receive_until(link, b'"firmware":"esp-hi-agent"', 5)
        link.write(wire)
        receive_until(link, ("@provision_ok " + config["nonce"]).encode(), 10)
    print("Provisioning acknowledged. No credential files were saved on this computer.")


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print("Provisioning cancelled.")
        raise SystemExit(1)
    except Exception as error:
        # Exceptions must not contain the request, configuration or credentials.
        print(f"Provisioning failed ({type(error).__name__}); check device/port and retry.")
        raise SystemExit(1)

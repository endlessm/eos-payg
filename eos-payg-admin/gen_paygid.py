#!/bin/env python3

import argparse
import http.client
import json
import syslog
import time


class GenPAYGUnits():
    base_url = "api.angaza.com"
    client_id = ""
    secret = ""
    org_token = ""
    access_token = ""
    product_qid = "PT000421"

    def __init__(self, client_id, secret, org_token):
        self.client_id = client_id
        self.secret = secret
        self.org_token = org_token
        self.Login()


    def Login(self):
        payload = f"{{\"clientId\":\"{self.client_id}\",\"secret\":\"{self.secret}\"}}"

        headers = {
            'Content-Type': "application/json",
            'Accept': "application/json",
            'X-Api-Key': self.org_token,
        }

        conn = http.client.HTTPSConnection(self.base_url)
        conn.request("POST", "/identity/resources/auth/v2/api-token", payload, headers)

        res = conn.getresponse()
        res_data = res.read()
        data = json.loads(res_data.decode("utf-8"))
        self.access_token = data["access_token"]
        syslog.syslog(syslog.LOG_INFO, "Login Angaza successfully!")


    def Generate_Units(self, count=1):
        conn = http.client.HTTPSConnection(self.base_url)

        headers = {
            'Content-Type': "application/json",
            'Accept': "application/json",
            'Authorization': "Bearer " + self.access_token,
            'X-Api-Key': self.org_token,
        }
        payload = f"{{\"product_qid\":\"{self.product_qid}\",\"count\":{count}}}"

        conn.request("POST", "/nexus/v2/units", payload, headers=headers)

        res = conn.getresponse()
        res_data = res.read()
        data = json.loads(res_data.decode("utf-8"))

        if res.status >= 400:
            err = f"Anagza fails to generate new PAYG ID with returned HTTP status code {res.status}"
            syslog.syslog(syslog.LOG_ERR, err)
            raise Exception(err)

        completed = data["completed_when"] is not None
        if completed:
            url = data["_links"]["result"]["href"]
            msg = f"Angaza has generated a new PAYG ID list at {url}"
        else:
            url = data["_links"]["self"]["href"]
            msg = f"Angaza is generating new PAYG IDs. Please check {url}"
        syslog.syslog(syslog.LOG_DEBUG, msg)

        return url, completed


    def Wait_Complete(self, url):
        data = {"completed_when": None}

        while data["completed_when"] is None:
            # Wait longer for Angaza generating PAYG IDs & Keys
            time.sleep(10)

            headers = {
                'Accept': "application/json",
                'Authorization': "Bearer " + self.access_token,
                'X-Api-Key': self.org_token,
            }
            conn = http.client.HTTPSConnection(self.base_url)
            conn.request("GET", url, headers=headers)

            res = conn.getresponse()
            res_data = res.read()
            data = json.loads(res_data.decode("utf-8"))

            if res.status >= 400:
                err = f"Anagza fails to generate new PAYG ID with returned HTTP status code {res.status}"
                syslog.syslog(syslog.LOG_ERR, err)
                raise Exception(err)

        return data["_links"]["result"]["href"]


    def Get_CSV(self, url):
        headers = {
            'Authorization': "Bearer " + self.access_token,
            'X-Api-Key': self.org_token,
        }
        conn = http.client.HTTPSConnection(self.base_url)
        conn.request("GET", url, headers=headers)

        res = conn.getresponse()
        if res.status in (301, 302, 303, 307, 308):
            uri = res.getheader('Location')
            print(f"Please download PAYG IDs & keys from {uri}")
        else:
            res_data = res.read()
            data = json.loads(res_data.decode("utf-8"))
            print(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Generate new PAYG ID and keys with Angaza version 2 API endpoint')
    parser.add_argument('--client-id', type=str, required=True, help='Angaza API client ID')
    parser.add_argument('--secret', type=str, required=True, help='Angaza API secret')
    parser.add_argument('--org-token', type=str, required=True, help='Angaza organization token')
    parser.add_argument('--amount', type=int, default=1, help='Number of PAYG units to generate (default: 1)')

    args = parser.parse_args()

    genPAYGUnits = GenPAYGUnits(args.client_id, args.secret, args.org_token)

    url, completed = genPAYGUnits.Generate_Units(args.amount)
    if not completed:
        print("Angaza is generating a new PAYG list and takes time.  Please wait ...")
        url = genPAYGUnits.Wait_Complete(url)
    genPAYGUnits.Get_CSV(url)

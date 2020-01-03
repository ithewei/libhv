#!/usr/bin/python3

import sys
import json
import requests

headers = { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36',
            'Cache-Control': 'no-cache',
            'Accpet': '*/*',
            'Content-Type': 'application/json'}

_node = {'ip': '127.0.0.1', 'port': 8500}

def gen_ServiceID(service_name, service_ip, service_port):
    return service_name + '-' + service_ip + ':' + str(service_port)

def register_service(service_name, service_ip, service_port , node=_node):
    service = {}
    service['Name'] = service_name
    service['Address'] = service_ip
    service['Port'] = service_port
    service['ID'] = gen_ServiceID(service_name, service_ip, service_port)
    check = {}
    check['TCP'] = service['Address'] + ':' + str(service['Port'])
    check['Interval'] = '10s'
    check['DeregisterCriticalServiceAfter'] = '30s'
    service['Check'] = check
    data = json.dumps(service)
    url = 'http://' + node['ip'] + ':' + str(node['port'])
    url += '/v1/agent/service/register'
    try:
        res = requests.put(url=url, headers=headers, data=data)
        return res.status_code == 200 and len(res.content) == 0
    except Exception as e:
        print(e)
        return False

def deregister_service(ServiceID, node=_node):
    url = 'http://' + node['ip'] + ':' + str(node['port'])
    url += '/v1/agent/service/deregister/'
    url += ServiceID
    try:
        res = requests.put(url=url, headers=headers)
        return res.status_code == 200 and len(res.content) == 0
    except Exception as e:
        print(e)
        return False

def discover_service(service_name, node=_node):
    url = 'http://' + node['ip'] + ':' + str(node['port'])
    url += '/v1/catalog/service/'
    url += service_name
    try:
        res = requests.get(url=url, headers=headers)
        return res.content
    except Exception as e:
        print(e)
        return 'false'

def main():
    print(sys.argv)
    if len(sys.argv) < 4:
        print('Usage: cmd service_name service_ip service_port node_ip node_port')
        return
    service_name = sys.argv[1]
    service_ip = sys.argv[2]
    service_port = int(sys.argv[3])
    if len(sys.argv) >= 5:
        _node['ip'] = sys.argv[4]
        _node['port'] = int(sys.argv[5])
    ServiceID = gen_ServiceID(service_name, service_ip, service_port)
    if register_service(service_name, service_ip, service_port):
        print('register_service [%s] succeed!' % (ServiceID))
    else:
        print('register_service failed!')

    print(discover_service(service_name))

    '''
    if deregister_service(ServiceID):
        print('deregister_service [%s] succeed!' % (ServiceID))
    else:
        print('deregister_service failed')

    print(discover_service(service_name))
    '''

if __name__ == '__main__':
    main()

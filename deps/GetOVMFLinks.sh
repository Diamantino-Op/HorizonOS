#!/bin/bash

expireTime="$(date -d 'now - 1 days' +%s)"
fileTime="$(date -r /tmp/ovmf-json +%s)"

if [ ! -e /tmp/ovmf-json ] || ((fileTime <= expireTime)); then
  curl https://api.github.com/repos/osdev0/edk2-ovmf-nightly/releases/latest > /tmp/ovmf-json
fi

cat /tmp/ovmf-json | jq ".assets[] | select (.name==\"ovmf-$1-$2.fd\")" | jq '.browser_download_url'


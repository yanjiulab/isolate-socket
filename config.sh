#!/bin/bash

# Create br0
# `br-exists BRIDGE` return 0 if BRIDGE exists
ovs-vsctl br-exists br0 
if [ $? -eq 0 ]; then
    echo 'br0 already exists ...'
    ovs-vsctl del-br br0
    echo 'Delete br0 ...'
fi
ovs-vsctl add-br br0
echo 'Create br0 ...'


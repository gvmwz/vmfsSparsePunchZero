# vmfsSparsePunchZero
This program can compact VMware ESXi `.vmdk` files just like `vmkfstools -K` does except for snapshots. Make sure you backup all your data and run this program in the same directory as the snapshot, then replace `snapshot-delta.vmdk` with the newly generated disk file, and voila. 

## Example
```
/vmfs/volumes/96a9f004-ff4149f8-9170-0e65721dd606/testvm # ./vmfsSparsePunchZero "testvm-000001.vmdk"
testvm-000001.vmdk:1133452/62914560 VMFSSPARSE
testvm.vmdk:62914560/62914560 VMFS
/vmfs/volumes/96a9f004-ff4149f8-9170-0e65721dd606/testvm # mv "testvm-000001-delta.vmdk.new" "testvm-000001-delta.vmdk"
```

## Install
Prebuilt binary release is available at [Releases](https://github.com/gvmwz/vmfsSparsePunchZero/releases/latest), or you can build from source.

## Build
```
git clone https://github.com/gvmwz/vmfsSparsePunchZero.git
cd vmfsSparsePunchZero && make
```
(Note: GCC 4.9 or newer is required)

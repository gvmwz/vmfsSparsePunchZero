# vmfsSparsePunchZero
This program can compact VMware ESXi `.vmdk` files just like `vmkfstools -K` does except for snapshots. Make sure you backup all your data and run this program in the same directory as the snapshot, then replace `snapshot-delta.vmdk` with the newly generated disk file, and voila. 

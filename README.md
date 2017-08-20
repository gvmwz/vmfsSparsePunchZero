# vmfsSparsePunchZero
This program can compact ESXi vmdk just like vmkfstools -K expect for snapshot. Make sure you backup all your data and run this program in the same directory as the snapshot, then replace "snapshot-delta.vmdk" with the newly generated disk file, and voila. 

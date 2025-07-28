savedcmd_fs/zonefs/zonefs.mod := printf '%s\n'   super.o file.o sysfs.o hodo.o | awk '!x[$$0]++ { print("fs/zonefs/"$$0) }' > fs/zonefs/zonefs.mod

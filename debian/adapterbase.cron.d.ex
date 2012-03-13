#
# Regular cron jobs for the adapterbase package
#
0 4	* * *	root	[ -x /usr/bin/adapterbase_maintenance ] && /usr/bin/adapterbase_maintenance

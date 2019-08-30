#!/usr/bin/env python2
#-*- coding:utf-8 -*-

###################
#
# Imports

import config
import models
import warmama

import MySQLdb
import sys
import string

###################
#
# Classes

if __name__ == '__main__' :
	connection = MySQLdb.connect ( host = "localhost",
									user = config.db_user,
									passwd = config.db_passwd,
									db = config.db_name )
	cursor = connection.cursor ()
	
	table_awards = models.table_Awards()
	table_awards.cursor = cursor
	
	rows = table_awards.select2("*", "1", ())
	for r in rows :
		clean_name = table_awards.GetCleanAwardName(r[1])
		print "%s %s" % (r[1], clean_name)

		table_awards.update("clean_name=%s", "id=%s", ( clean_name, r[0] ))
		connection.commit()

		try:
			table_awards.update("uniq_name=%s", "id=%s", ( clean_name, r[0] ))
			connection.commit()
		except:
			pass
#!/usr/bin/env python3
# call with <name> <indent count>
import sys
names= {}
def process(line):
	if line.find('=')==-1:
		return
	left,right= [x.strip() for x in line.split('=')]
	if right[-1] == ',':
		right= right[:-1]
	if right[0].isalpha():
		names[right].append(left)
	else:
		names[left]= []
try:
	while True:
		process(input())
except EOFError:
	pass
for x,y in names.items():
	print(int(sys.argv[1])*'\t' + '{', x, ', "', x, sep='', end='')
	if y:
		print(' (aka ', end='')
		isInitial= True
		for z in y:
			if isInitial:
				isInitial= False
			else:
				print(', ', end='')
			print(z, end='')
		print(')', end='')
	print('"},')

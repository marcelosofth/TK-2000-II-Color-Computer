import re
cs = open(r'G:\Internet_Temp\Batocera_add\tk2000\src\Credits.cpp', encoding='latin-1').read()
lines = re.findall(r'"([^"]*)"', re.search(r'kCreditsLines\[\] = \{(.*?)\n\};', cs, re.S).group(1))
print('total de linhas: %d' % len(lines))
bad = [(len(l), l) for l in lines if len(l) > 42]
print('linhas > 42 chars:', bad if bad else 'nenhuma')
print('maximo: %d' % max(len(l) for l in lines))

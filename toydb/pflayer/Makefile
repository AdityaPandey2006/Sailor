#PUBLICDIR= /usr0/cs564/public/project
SRC= buf.c hash.c pf.c
OBJ= buf.o hash.o pf.o
HDR = pftypes.h pf.h 

pflayer.o: $(OBJ)
	ld -r -o pflayer.o $(OBJ)

tests: testhash testpf testcapacity testreplacement testdirty teststats

teststats: teststats.o pflayer.o
	$(CC) -o teststats teststats.o pflayer.o

benchmark: benchmark.o pflayer.o
	$(CC) -o benchmark benchmark.o pflayer.o

teststats.o benchmark.o: $(HDR)

testdirty: testdirty.o pflayer.o
	$(CC) -o testdirty testdirty.o pflayer.o

testdirty.o: $(HDR)

testreplacement: testreplacement.o pflayer.o
	$(CC) -o testreplacement testreplacement.o pflayer.o

testreplacement.o: $(HDR)

testcapacity: testcapacity.o pflayer.o
	$(CC) -o testcapacity testcapacity.o pflayer.o

testcapacity.o: $(HDR)

testpf: testpf.o pflayer.o
	cc -o testpf testpf.o pflayer.o

testhash: testhash.o pflayer.o
	cc -o testhash testhash.o pflayer.o

$(OBJ): $(HDR)

testhash.o: $(HDR)

testpf.o: $(HDR)

lint: 
	lint $(SRC)

install: pflayer.o 


C++ = g++
CCFLAGS = -I../../../common -shared -fPIC -O3 -g

all: randwriter.so sort.so sorthash.so wordbucket.so gen_idx.so mr_sort.so mr_word.so

randwriter.so: randwriter.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

sort.so: sort.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

sorthash.so: sorthash.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

wordbucket.so: wordbucket.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

gen_idx.so: gen_idx.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

mr_sort.so: mr_sort.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

mr_word.so: mr_word.cpp
	$(C++) $(CCFLAGS) -o $@ $^ -lstdc++

clean:
	rm -f *.so

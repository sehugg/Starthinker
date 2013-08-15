
all:
	cd src && make && cd ../games && make && cd ..

clean:
	cd src && make clean && cd ../games && make clean && cd ..
	
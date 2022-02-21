server:	main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h
	g++ -o server main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h -pthread 

clean:
	rm -r server
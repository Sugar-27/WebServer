server:	main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./log/log.h ./log/log.cpp ./log/block_queue.h ./Connection_pool/connection.h ./Connection_pool/connectionPool.h ./md5/md5.h
	g++ -o server main.cpp ./http/http_conn.cpp ./timer/timer.cpp ./log/log.cpp ./Connection_pool/connection.cpp ./Connection_pool/connectionPool.cpp ./md5/md5.cpp -pthread -lmysqlclient

clean:
	rm -r server
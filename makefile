server:	main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./log/log.h ./log/log.cpp ./log/block_queue.h ./Connection_pool/connection.h ./Connection_pool/connectionPool.h
	g++ -o server main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./log/log.h ./log/log.cpp ./log/block_queue.h ./Connection_pool/connection.h ./Connection_pool/connection.cpp ./Connection_pool/connectionPool.h ./Connection_pool/connectionPool.cpp -pthread -lmysqlclient

clean:
	rm -r server
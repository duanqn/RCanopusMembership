ssh $1@$2 "ulimit -Hc unlimited && ulimit -Sc 52000 && cd $3 && ./$4 > $5" &
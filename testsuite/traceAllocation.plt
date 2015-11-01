set style data line
set xlabel 'allocation/free index'
set ylabel 'bytes allocated'
set term png
set output 'traceAllocation.png'
plot 'traceAllocation.dat'

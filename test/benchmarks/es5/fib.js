var print = globalThis.print || console.log;
function clock() {
  return performance.now() / 1000;
}
function idiv(a, b) {
  return Math.floor(a / b);
}
function error(e) {
  throw e;
}

function fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

var start = clock();

fib(36);

print(clock() - start);

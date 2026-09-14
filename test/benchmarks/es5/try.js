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

function ticker(n) {
  try {
  } catch (e) {}
  try {
  } catch (e) {}
  try {
  } catch (e) {}
  try {
  } catch (e) {}

  try {
    try {
      try {
        try {
          if (n < 0) return ticker(n - 1);
          throw "boom!";
        } catch (e) {
          throw e;
        }
      } catch (e) {
        throw e;
      }
    } catch (e) {
      throw e;
    }
  } catch (e) {
    throw e;
  }
}

var start = clock();

for (var i = 0; i < 10000000; i++)
  try {
    ticker(0x400);
  } catch (e) {
    if (e !== "boom!") throw 'e !== "boom!"';
  }

print(clock() - start);

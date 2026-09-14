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

Array.prototype.reverse = function () {
  for (
    var i = 0, j = this.length - 1, count = idiv(this.length, 2);
    i < count;
    i++, j--
  ) {
    var temp = this[i];
    this[i] = this[j];
    this[j] = temp;
  }
  return this;
};

Array.prototype.map = function (f) {
  for (var i = 0, count = this.length; i < count; i++) {
    this[i] = f(this[i], i, this);
  }
  return this;
};

var LIMIT = 5000000;
var a = new Array();

var start = clock();

for (var i = 0; i < LIMIT; i++) a.push(i);

a.map(function (el) {
  return el * 2;
}).reverse();

for (var i = LIMIT - 1; i >= 0; i--)
  if (a.pop() !== (LIMIT - i - 1) * 2) error("a.pop() !== (LIMIT - i - 1) * 2");

if (a.length !== 0) error("a.length !== 0");

print(clock() - start);

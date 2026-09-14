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

function Tree(depth) {
  this.depth = depth;
  if (depth > 0) {
    this.a = new Tree(depth - 1);
    this.b = new Tree(depth - 1);
    this.c = new Tree(depth - 1);
    this.d = new Tree(depth - 1);
    this.e = new Tree(depth - 1);
  }
  return this;
}

Tree.prototype.walk = function () {
  if (this.depth === 0) return 0;
  return (
    this.depth +
    this.a.walk() +
    this.b.walk() +
    this.c.walk() +
    this.d.walk() +
    this.e.walk()
  );
};

var tree = new Tree(9);

var start = clock();

for (var i = 0; i < 10; i++) {
  if (tree.walk() !== 610349) error("tree.walk() !== 610349");
}

print(clock() - start);

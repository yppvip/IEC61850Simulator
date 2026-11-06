// Optional offline interaction check. Run with Node.js, without a browser or SCD file.
const assert = require("node:assert/strict");
const {readFileSync} = require("node:fs");
const {resolve} = require("node:path");
const {runInNewContext} = require("node:vm");

class Element {
  constructor(tag = "") {
    this.tag = tag;
    this.children = [];
    this.attributes = {};
    this.listeners = {};
    this.classes = new Set();
    this.classList = {
      add: name => this.classes.add(name),
      remove: name => this.classes.delete(name),
      toggle: (name, on) => on ? this.classes.add(name) : this.classes.delete(name)
    };
    this.textContent = "";
    this.isConnected = true;
  }
  append(item) { this.children.push(item); }
  replaceChildren() {
    for (const item of this.children) item.isConnected = false;
    this.children = [];
  }
  setAttribute(key, value) { this.attributes[key] = value; }
  addEventListener(name, handler) { this.listeners[name] = handler; }
  dispatch(name, event = {}) { this.listeners[name](event); }
  getComputedTextLength() { return this.textContent.length * 9; }
  querySelector(tag) { return this.children.find(item => item.tag === tag); }
  closest() { return null; }
  setPointerCapture() {}
  focus() {}
  select() {}
}

const nodes = Array.from({length: 101}, (_, id) => ({
  title: id === 1 ? "<unsafe & \"quoted\">" : "Node " + id,
  lane: ["declaration", "evidence", "derived"][id % 3],
  line: id + 1,
  excerpt: [{line: id + 1, text: "<DO/>"}],
  copy: id === 1 ? "完整引用" : ""
}));
const edge = to => ({from: 0, to, kind: "reference", label: "引用"});
const groups = Array.from({length: 12}, (_, i) => ({
  label: "Group " + i,
  total: i === 0 ? 29 : 8,
  edges: Array.from({length: i === 0 ? 24 : 8}, (_, j) => edge(i === 0 ? j + 1 : 1 + i * 8 + j))
}));
const graph = {version: 1, file: "Synthetic.scd", sha256: "test", focus: 0, nodes,
  visibleLimit: 80, views: {0: groups, 1: []}};
const page = readFileSync(resolve(__dirname, "../../src/core/SclTopology.html"), "utf8");
const elements = new Map([...page.matchAll(/\bid="([^"]+)"/g)].map(([, id]) => [id, new Element()]));
elements.get("graph-data").textContent = Buffer.from(JSON.stringify(graph)).toString("base64");
const get = id => elements.get(id);
const canvas = get("canvas");
canvas.createSVGPoint = () => ({matrixTransform() { return this; }});
canvas.getScreenCTM = () => ({inverse() {}});
runInNewContext(page.match(/<script>\s*([\s\S]*?)<\/script>/)[1], {
  document: {
    getElementById: get,
    createElement: tag => new Element(tag),
    createElementNS: (_, tag) => new Element(tag)
  },
  atob, TextDecoder, Uint8Array
});

assert.match(get("notice").textContent, /5 条关系未载入/);
assert.match(get("notice").textContent, /画布另省略/);
assert.equal(get("drawing").children.filter(el => el.attributes.class?.startsWith("node ")).length, 80);
const group = () => get("groups").children[0];
const rows = () => group().children[1];
rows().children[0].children[1].dispatch("click"); // Next page.
assert.match(rows().children[1].textContent, /Node 9/);
rows().children[0].children[0].dispatch("click"); // Previous page.
rows().children[1].dispatch("click"); // Keyboard-accessible edge button.
assert.match(get("details").children[0].textContent, /Group 0 · 引用/);
get("details").children[2].dispatch("click"); // Inspect edge endpoint.
assert.equal(get("details").children[0].textContent, nodes[1].title);
get("details").children[1].children[0].dispatch("click"); // Recenter.
assert.equal(get("focus").textContent, "焦点 · " + nodes[1].title);
get("back").dispatch("click");
assert.match(get("focus").textContent, /Node 0/);
group().open = false;
group().dispatch("toggle");
assert.equal(group().open, false);
const fitted = canvas.attributes.viewBox;
get("plus").dispatch("click");
assert.notEqual(canvas.attributes.viewBox, fitted);
get("fit").dispatch("click");
assert.equal(canvas.attributes.viewBox, fitted);
canvas.dispatch("pointerdown", {button: 0, target: canvas, pointerId: 1, clientX: 5, clientY: 6});
canvas.dispatch("pointermove", {pointerId: 1, clientX: 15, clientY: 16});
assert.notEqual(canvas.attributes.viewBox, fitted);
canvas.dispatch("lostpointercapture");
assert.equal(canvas.classes.has("dragging"), false);
console.log("SCL topology SVG interactions passed.");

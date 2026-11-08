// Runs the dashboard script against a minimal DOM and a canned API, so the tests read what it renders.
import vm from 'node:vm';

import { ADMIN_HTML, ADMIN_JS } from '../src/admin_page.js';

class FakeNode {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.className = '';
    this.attrs = {};
    this.listeners = {};
    this.hidden = false;
    this.value = '';
    this.checked = false;
    this.own = '';
  }

  get textContent() {
    return this.children.length ? this.children.map((c) => c.textContent).join('') : this.own;
  }

  set textContent(value) {
    this.children = [];
    this.own = String(value);
  }

  get firstChild() {
    return this.children[0] ?? null;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  removeChild(child) {
    this.children = this.children.filter((c) => c !== child);
  }

  setAttribute(name, value) {
    this.attrs[name] = value;
  }

  addEventListener(type, fn) {
    (this.listeners[type] ??= []).push(fn);
  }

  click() {
    for (const fn of this.listeners.click ?? []) fn({ preventDefault() {} });
  }

  focus() {}

  find(predicate, found = []) {
    if (predicate(this)) found.push(this);
    for (const child of this.children) child.find(predicate, found);
    return found;
  }

  rows() {
    return this.find((n) => n.tag === 'tr').map((tr) => tr.children.map((c) => c.textContent));
  }
}

export const settle = async () => {
  for (let i = 0; i < 30; i++) await new Promise((resolve) => setImmediate(resolve));
};

// respond(path, init) answers like fetch, as a value or a promise: { status, body, headers }.
export async function openDashboard(respond, { signIn = true, now = Date.now() } = {}) {
  const nodes = new Map();
  for (const id of ADMIN_HTML.matchAll(/id="([^"]+)"/g)) nodes.set(id[1], new FakeNode('div'));
  const requests = [];
  const downloads = [];
  const clipboard = [];
  const sandbox = {
    document: {
      getElementById: (id) => nodes.get(id) ?? null,
      createElement: (tag) => {
        const node = new FakeNode(tag);
        if (tag === 'a') node.click = () => downloads.push({ href: node.href, download: node.download });
        return node;
      },
    },
    navigator: { clipboard: { writeText: async (text) => { clipboard.push(text); } } },
    sessionStorage: { getItem: () => '', setItem() {}, removeItem() {} },
    URL: { createObjectURL: () => 'blob:test', revokeObjectURL() {} },
    setTimeout: () => 1,
    fetch: async (path, init = {}) => {
      requests.push({ path, init });
      const { status = 200, body = {}, headers = {} } = await respond(path, init);
      const text = typeof body === 'string' ? body : JSON.stringify(body);
      return {
        status, ok: status >= 200 && status < 300, json: async () => JSON.parse(text), text: async () => text,
        blob: async () => text, headers: { get: (name) => headers[name.toLowerCase()] ?? null },
      };
    },
    Intl, Math, Date: class extends Date { static now() { return now; } }, Number, JSON, Promise, Error, Object, Array,
  };
  vm.runInNewContext(ADMIN_JS, sandbox);
  if (signIn) {
    nodes.get('token').value = 'admin-token-0123456789-abcdefgh';
    for (const fn of nodes.get('login').listeners.submit) fn({ preventDefault() {} });
    await settle();
  }
  return {
    nodes, requests, downloads, clipboard,
    text: (id) => nodes.get(id).textContent,
    rows: (id) => nodes.get(id).rows(),
    all: () => [...nodes.values()].map((n) => n.textContent).join('\n'),
  };
}

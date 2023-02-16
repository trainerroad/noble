const { EventEmitter } = require('events');
const { inherits } = require('util');
const { resolve } = require('path');
const dir = resolve(__dirname, '..', '..');
const bindings = require('bindings');

const { NobleWinrt } = bindings('binding.node');

inherits(NobleWinrt, EventEmitter);

module.exports = NobleWinrt;

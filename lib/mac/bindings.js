const { EventEmitter } = require('events');
const { inherits } = require('util');
const { resolve } = require('path');
const dir = resolve(__dirname, '..', '..');
const bindings = require('bindings');

const { NobleMac } = bindings('binding.node');

inherits(NobleMac, EventEmitter);

module.exports = NobleMac;

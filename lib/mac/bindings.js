const { EventEmitter } = require('events');
const { inherits } = require('util');
const bindings = require('bindings');

const { NobleMac } = bindings('binding.node');

inherits(NobleMac, EventEmitter);

module.exports = NobleMac;

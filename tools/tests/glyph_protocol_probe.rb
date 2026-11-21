#!/usr/bin/env ruby
# frozen_string_literal: true

# Run inside a Blit terminal session by glyph_protocol_gate.rb: a program on
# the terminal, with nothing but its PTY. For each case it writes the
# request, then DA1 (CSI c), and records every byte the terminal sent back
# before DA1's reply, and whatever came after it, in hex: the Glyph
# Protocol's own detection rule (its §3.4) -- what was answered by the time
# DA1 is answered is all there is. Bytes are read on a thread of their own
# the whole time, so a burst of replies never fills the PTY's input buffer
# while a request is still being written.
#
#   glyph_protocol_probe.rb CASES.json RESULTS.json
#
# CASES.json: [{"hex": "<request bytes>", "timeout": seconds}, ...]

require 'json'

DA1_REPLY = /\e\[\?[0-9;]*c/n

cases = JSON.parse(File.read(ARGV.fetch(0)))
results_path = ARGV.fetch(1)
system('stty raw -echo < /dev/tty') || abort('stty failed')
tty = File.open('/dev/tty', 'r+b')
received = String.new(encoding: Encoding::BINARY)
lock = Mutex.new
Thread.new do
  loop do
    chunk = tty.readpartial(65_536)
    lock.synchronize { received << chunk }
  rescue EOFError, IOError
    break
  end
end

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

results = cases.map do |test|
  request = [test.fetch('hex')].pack('H*')
  start = lock.synchronize { received.bytesize }
  began = now
  tty.write(request + "\e[c")
  tty.flush
  deadline = began + test.fetch('timeout', 5)
  match = nil
  got = nil
  loop do
    got = lock.synchronize { received.byteslice(start..) }
    match = got.match(DA1_REPLY)
    break if match || now > deadline

    sleep 0.005
  end
  elapsed = ((now - began) * 1000).round
  if match
    # Anything a reply sends after DA1's is out of order; give it a moment.
    sleep 0.05
    got = lock.synchronize { received.byteslice(start..) }
    { 'before' => got.byteslice(0, match.begin(0)).unpack1('H*'),
      'after' => got.byteslice(match.end(0)..).unpack1('H*'), 'ms' => elapsed }
  else
    { 'before' => nil, 'after' => got.unpack1('H*'), 'ms' => elapsed }
  end
end

File.write("#{results_path}.part", JSON.generate(results))
File.rename("#{results_path}.part", results_path)

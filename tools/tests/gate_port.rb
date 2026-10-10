# frozen_string_literal: true

# Issue #28. A gate that spawns its own server has to know its port is free
# before it binds, because the failure otherwise lands on the gate rather than
# on the sequencing: run two gates back to back and one can reach listen()
# while the previous server still holds the socket, which looks exactly like
# the second gate being broken and disappears when you re-run it alone.
#
# This is a connect test rather than a bind test on purpose. What matters is
# whether something is already *serving* on the port; a port left in TIME_WAIT
# by a client socket does not stop a listener and must not stop a gate.
#
# Every gate's port is still overridable through its own documented variable;
# this only decides whether the chosen one is usable, and says who has it when
# it is not.

require 'socket'

GATE_PORT_TIMEOUT = Float(ENV.fetch('BLIT_GATE_PORT_TIMEOUT', '15'))

def gate_port_holder(port)
  listing = `ss -ltnp 2>/dev/null || netstat -ltnp 2>/dev/null`
  listing.lines.grep(/[:.]#{port}\s/).map(&:strip).join('; ')
rescue StandardError
  ''
end

def gate_port(port, name = File.basename($PROGRAM_NAME))
  port = Integer(port)
  deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + GATE_PORT_TIMEOUT
  waited = false
  loop do
    begin
      TCPSocket.new('127.0.0.1', port, connect_timeout: 1).close
    rescue Errno::ECONNREFUSED, Errno::EHOSTUNREACH, Errno::ETIMEDOUT,
           SocketError, IO::TimeoutError
      warn "#{name}: port #{port} became free" if waited
      return port
    rescue StandardError
      # Anything else is not evidence that the port is taken.
      return port
    end
    if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
      holder = gate_port_holder(port)
      abort("#{name}: TCP port #{port} is still serving after " \
            "#{GATE_PORT_TIMEOUT.round}s, so this gate cannot bind it. " \
            'This is a port conflict, not a failure of what the gate ' \
            "asserts.#{holder.empty? ? '' : " Held by: #{holder}."} " \
            "Override with its port variable, or stop the holder.")
    end
    waited = true
    sleep 0.25
  end
end

#!/usr/bin/env ruby
# frozen_string_literal: true

# Exercise the existing operator sixel assertions through one explicit SDL
# profile. Local and physical runs share the protocol/pixel oracle.
require 'json'
require 'net/http'
require 'open3'
require 'timeout'
require_relative 'gate_port'
require_relative 'test_profile'

port = gate_port(Integer(ENV.fetch('BLIT_SIXEL_COMPOSITION_PORT', '19304')))
base = "http://127.0.0.1:#{port}"
log = "/tmp/blit-sixel-composition-#{Process.pid}.log"
pid = Process.spawn(TestProfile.environment, TestProfile.binary, port.to_s,
                    out: log, err: [:child, :out])
begin
  diagnostics = Timeout.timeout(10) do
    loop do
      begin
        response = Net::HTTP.get_response(URI(base + '/v1/diagnostics'))
        break JSON.parse(response.body) if response.code == '200'
      rescue SystemCallError, IOError
        nil
      end
      sleep 0.05
    end
  end
  TestProfile.verify!(diagnostics)
  topology = JSON.parse(Net::HTTP.get(URI(base + '/v1/topology')))
  request = Net::HTTP::Put.new('/v1/topology')
  request['Content-Type'] = 'application/json'
  request['If-Match'] = topology.fetch('revision').to_s
  request.body = JSON.generate(revision:topology.fetch('revision'), layout:{columns:1,rows:2})
  response = Net::HTTP.start('127.0.0.1', port) { |http| http.request(request) }
  raise "split HTTP #{response.code}" unless response.code == '200'
  panes = JSON.parse(response.body).fetch('panes').sort_by { |pane| pane.dig('pixel_region','y') }
  request = Net::HTTP::Post.new('/v1/terminal-sessions')
  request['Content-Type'] = 'application/json'
  request.body = JSON.generate(command:'/bin/sh',arguments:['-c','sleep 120'],pane_id:panes.first.fetch('id'))
  response = Net::HTTP.start('127.0.0.1', port) { |http| http.request(request) }
  raise "guard session HTTP #{response.code}" unless response.code == '201'
  output, error, status = Open3.capture3(
    {'BLIT_LIVE'=>base, 'BLIT_AUTH_TOKEN'=>'', 'BLIT_EVIDENCE_DIR'=>nil,
     'BLIT_SIXEL_PANE_ID'=>panes.last.fetch('id')},
    'ruby', File.expand_path('../probes/host_sixel.rb', __dir__))
  raise "sixel probe failed:\n#{output}#{error}" unless status.success?
  puts "SIXEL_COMPOSITION_PASS backend=#{GATE_BACKEND} #{output.lines.last.to_s.strip}"
ensure
  Process.kill('TERM', pid) rescue nil
  Process.wait(pid) rescue nil
end

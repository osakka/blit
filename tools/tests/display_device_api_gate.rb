#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0064 stage 6 (#54). A display is a device in the same envelope as an
# input device and an audio device, with an item endpoint and a real
# collection revision.
#
# What it deliberately does not have is a writable property, and this gate
# asserts that too. Selecting a mode changes the render output size and
# therefore every surface on the display, and a Hibr desktop's cells come
# from its owner (ADR 0063), so Blit cannot repaint one at a new size by
# itself: it needs a resize protocol with the owner, which is its own ticket.
# Reporting the modes and refusing the write is honest; a `mode` property that
# accepted a value and did nothing would not be.
#
# Also checked: the dead seventh argument this function used to pass to a
# six-specifier format string, by asserting the bounds it reports are the
# bounds it has rather than the height twice.

require 'json'
require 'net/http'
require 'timeout'
require 'uri'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
PORT = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19250'), 10))
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
FONT = ENV.fetch('BLIT_GATE_FONT',
                 File.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'))
BASE = URI("http://127.0.0.1:#{PORT}")

abort("display device api gate: missing #{BINARY}") unless File.executable?(BINARY)

failures = []

def request(method, path, body: nil)
  uri = BASE + path
  klass = { 'GET' => Net::HTTP::Get, 'PATCH' => Net::HTTP::Patch }.fetch(method)
  req = klass.new(uri)
  if body
    req['Content-Type'] = 'application/json'
    req.body = JSON.generate(body)
  end
  response = Net::HTTP.start(uri.hostname, uri.port, read_timeout: 20) do |http|
    http.request(req)
  end
  parsed = begin
    response.body.to_s.empty? ? nil : JSON.parse(response.body)
  rescue JSON::ParserError
    response.body
  end
  [response.code.to_i, parsed]
end

server = spawn({ **gate_display_env, 'BLIT_PSF_FONT' => FONT,
                 },
               BINARY, PORT.to_s, out: '/dev/null', err: '/dev/null')
at_exit do
  Process.kill('TERM', server) rescue nil
  Process.wait(server) rescue nil
end

Timeout.timeout(15) do
  loop do
    begin
      break if request('GET', '/v1/version').first == 200
    rescue StandardError
      nil
    end
    sleep 0.2
  end
end

code, collection = request('GET', '/v1/displays')
failures << "GET /v1/displays -> #{code}" unless code == 200
displays = collection.is_a?(Hash) ? collection.fetch('displays') : []
failures << 'no displays reported' if displays.empty?
if collection.is_a?(Hash)
  failures << "count #{collection['count']} disagrees with #{displays.length} " \
              'serialised displays' unless collection['count'] == displays.length
  failures << "#{collection['dropped']} display(s) did not fit the response" unless
    collection['dropped'].to_i.zero?
end

envelope = %w[id class name bounds modes active_mode presentation_state
              capabilities state configuration revision]
displays.each do |display|
  missing = envelope.reject { |key| display.key?(key) }
  failures << "#{display['id']}: missing #{missing.inspect}" unless missing.empty?
  failures << "#{display['id']}: class #{display['class'].inspect}" unless
    display['class'] == 'display'
  bounds = display.fetch('bounds', {})
  %w[x y width height].each do |key|
    failures << "#{display['id']}: bounds missing #{key}: #{bounds.inspect}" unless
      bounds.key?(key)
  end
  # The width must be the width. The serialiser this replaced passed the
  # height twice to a format string with six specifiers, so the seventh
  # argument was dead -- a format change away from reporting height as width.
  if bounds['width'] == bounds['height'] && display['modes'].to_a.any? { |m|
       m['width'] != m['height'] }
  then
    failures << "#{display['id']}: square bounds #{bounds.inspect} while its " \
                'modes are not square, which is what a duplicated argument ' \
                'would look like'
  end
  # Issue #69 replaced the empty object, but not the reasoning behind it: a
  # display's `configuration` carries exactly the properties this backend and
  # this hardware can actually perform, and each one has to be claimed in
  # `capabilities` first. A property offered without the capability, or a
  # capability with nothing to set, is the lie the empty object avoided.
  settable = display.fetch('configuration', {}).keys
  claimed = display.fetch('capabilities', [])
  failures << "#{display['id']}: capabilities #{claimed.inspect} must include " \
              'mode-report' unless claimed.include?('mode-report')
  unexpected = settable - %w[rotation brightness_percent]
  failures << "#{display['id']}: configuration offers #{unexpected.inspect}; " \
              "mode selection still needs issue #60's resize protocol" unless
    unexpected.empty?
  failures << "#{display['id']}: rotation is settable without the capability" if
    settable.include?('rotation') && !claimed.include?('rotation')
  failures << "#{display['id']}: rotation is a capability but not settable" if
    claimed.include?('rotation') && !settable.include?('rotation')
  if settable.include?('brightness_percent') && !claimed.include?('brightness')
    failures << "#{display['id']}: brightness_percent is settable without " \
                'the capability'
  end
  failures << "#{display['id']}: state #{display['state'].inspect}" unless
    display.dig('state', 'present') == true
  failures << "#{display['id']}: revision #{display['revision'].inspect}" unless
    display['revision'].is_a?(Integer) && display['revision'] >= 1
end

first = displays.first
if first
  code, item = request('GET', "/v1/displays/#{first.fetch('id')}")
  failures << "GET the item -> #{code}" unless code == 200
  if item.is_a?(Hash) && item != first
    failures << 'the item endpoint disagrees with the collection: ' \
                "#{item.inspect} vs #{first.inspect}"
  end
  # A mode is the one that resizes every surface on the display, and it is
  # still issue #60's. The PATCH route exists now, so the refusal has to come
  # from the property check rather than from the route's absence -- a weaker
  # guarantee than it was, which is why the problem detail is read too.
  code, problem = request('PATCH', "/v1/displays/#{first.fetch('id')}",
                          body: { 'configuration' => {
                            'mode' => { 'width' => 640, 'height' => 480 } } })
  failures << "PATCH a mode -> #{code}; selecting a mode resizes every " \
              'surface and must still be refused' unless code == 400
  unless problem.is_a?(Hash) && problem['code'] == 'invalid_display_property'
    failures << "a refused mode should name the property: #{problem.inspect}"
  end
end

code, = request('GET', '/v1/displays/display-does-not-exist')
failures << "an unknown display id -> #{code}, wanted 404" unless code == 404

unless failures.empty?
  failures.each { |failure| warn "display device api gate: #{failure}" }
  exit 1
end

puts "DISPLAY_DEVICE_API_PASS displays=#{displays.length} envelope=11 " \
     'item_matches_collection=yes capabilities=derived mode=refused unknown_id=404'

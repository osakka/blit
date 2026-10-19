#!/usr/bin/env ruby
# frozen_string_literal: true

# Keep the machine-readable contract aligned with the implemented gateway
# routes. The OpenAPI document is authoritative; this gate only checks the
# routing invariants that a generic YAML parser can verify without generated
# bindings.

require 'yaml'

schema_path = ARGV.fetch(0, 'openapi/blit-0.1.yaml')
schema = YAML.load_file(schema_path)
raise 'schema is not OpenAPI 3.1' unless schema.fetch('openapi') == '3.1.0'

paths = schema.fetch('paths')
raise "unexpected path count #{paths.length}" unless paths.length == 45

settings = schema.fetch('components').fetch('schemas').fetch('Settings')
appearance = schema.fetch('components').fetch('schemas').fetch('PointerAppearance')
raise 'pointer appearance contract changed' unless
  appearance.dig('properties','shape','enum') == %w[arrow hand precision] &&
  appearance.dig('properties','palette','enum') == %w[pearl obsidian neon amber] &&
  appearance.dig('properties','size_px','minimum') == 24 &&
  appearance.dig('properties','size_px','maximum') == 64 &&
  appearance['additionalProperties'] == false &&
  settings.dig('properties','effective','required').include?('pointer')
raise 'settings durability receipt missing' unless settings['required'].include?('storage')
raise 'login policy schema missing' unless
  settings.dig('properties','document','properties','login','$ref') == '#/components/schemas/LoginPolicy'
raise 'effective login restart reporting missing' unless
  settings.dig('properties','effective','required').include?('login_restart_required')
policy = schema.fetch('components').fetch('schemas').fetch('LoginPolicy')
raise 'login fallback contract changed' unless
  policy.dig('properties','fallback','enum') == %w[first_available wait]
settings = schema.fetch('components').fetch('schemas').fetch('Settings')
auxiliary = settings.dig('properties','document','properties','auxiliary_displays')
raise 'saved auxiliary binding schema missing' unless auxiliary &&
  auxiliary['maxItems'] == 16 &&
  auxiliary.dig('items','$ref') == '#/components/schemas/AuxiliaryDisplayBinding'
binding = schema.fetch('components').fetch('schemas').fetch('AuxiliaryDisplayBinding')
raise 'auxiliary binding shape changed' unless binding['required'] == %w[output pane_ordinal] &&
  binding.dig('properties','pane_ordinal','maximum') == 64
layouts = settings.dig('properties','document','properties','display_layouts')
raise 'per-display layout bounds missing' unless layouts && layouts['maxProperties'] == 16 &&
  layouts.dig('additionalProperties','properties','columns','maximum') == 8 &&
  layouts.dig('additionalProperties','properties','rows','maximum') == 8
raise 'live auxiliary apply schema missing' unless
  paths.dig('/v1/auxiliary-displays/apply','post','responses','202','content','application/json','schema','$ref') ==
    '#/components/schemas/AuxiliaryDisplays'
login = schema.fetch('components').fetch('schemas').fetch('LoginManager')
raise 'login identity observation missing' unless login['required'].include?('identity') &&
  login.dig('properties','identity','$ref') == '#/components/schemas/LoginIdentity'
identity = schema.fetch('components').fetch('schemas').fetch('LoginIdentity')
raise 'login identity states changed' unless
  identity.dig('properties','state','enum') == %w[waiting verified rejected unavailable]
raise 'native desktop association missing' unless login['required'].include?('desktop') &&
  login.dig('properties','desktop','$ref') == '#/components/schemas/LoginDesktopAssociation'
desktop = schema.fetch('components').fetch('schemas').fetch('LoginDesktopAssociation')
raise 'native desktop association states changed' unless
  desktop.dig('properties','state','enum') == %w[inactive waiting ready error]
raise 'native client collection missing' unless desktop['required'].include?('clients') &&
  desktop.dig('properties','clients','maxItems') == 16 &&
  desktop.dig('properties','clients','items','properties','row','minimum') == -32767

observation_path = paths.fetch('/v1/diagnostics/observation')
observation_get = observation_path.fetch('get')
raise 'observation operation id changed' unless
  observation_get.fetch('operationId') == 'getObservation'
raise 'observation schema changed' unless
  observation_get.dig('responses', '200', 'content', 'application/json',
                       'schema', '$ref') == '#/components/schemas/Observation'
observation = schema.fetch('components').fetch('schemas').fetch('Observation')
render_output = schema.fetch('components').fetch('schemas').fetch('Diagnostics')
  .dig('properties', 'render', 'properties', 'outputs', 'items')
scanout = render_output&.dig('properties', 'kms_scanout')
raise 'per-output KMS scanout telemetry contract missing' unless scanout &&
  scanout['required'].sort == %w[available flip_completions vblank_sequence
                                  last_vblank_timestamp_us vblank_span_count
                                  vblank_span_us observed_refresh_hz
                                  pointer_change_requests pointer_frame_deferred
                                  pointer_frame_submissions].sort &&
  scanout['additionalProperties'] == false
required_observation_fields = %w[observation_version captured_monotonic_us
                                  consistency version server diagnostics runtime
                                  graphics displays audio audio_cards input_devices
                                  input_routes topology fonts settings login
                                  sessions effects overlays]
missing_observation_fields = required_observation_fields -
  observation.fetch('required')
raise "observation fields missing #{missing_observation_fields.inspect}" unless
  missing_observation_fields.empty?
raise 'server resource link for observation is missing' unless
  schema.fetch('components').fetch('schemas').fetch('ServerDetails')
    .dig('properties', 'resources', 'properties', 'observation')

session_path = paths.fetch('/v1/terminal-sessions/{session_id}')
hotplug = schema.fetch('components').fetch('schemas').fetch('Diagnostics')
  .dig('properties', 'render', 'properties', 'hotplug')
raise 'hotplug diagnostics contract missing' unless hotplug &&
  hotplug['required'].sort == %w[supported observer inventory_revision scans last_scan_ms error].sort &&
  hotplug.dig('properties','observer','enum').sort == ['unavailable','periodic','uevent+periodic'].sort &&
  hotplug['additionalProperties'] == false
raise 'session path must not accept terminal input directly' if session_path.key?('post')
raise 'session path lost GET' unless session_path.key?('get')
raise 'session path lost PATCH' unless session_path.key?('patch')
raise 'session path lost DELETE' unless session_path.key?('delete')

input_path = paths.fetch('/v1/terminal-sessions/{session_id}/input')
post = input_path.fetch('post')
raise 'terminal input operation id changed' unless post.fetch('operationId') == 'sendTerminalInput'
raise 'terminal input schema changed' unless
  post.dig('requestBody', 'content', 'application/json', 'schema', '$ref') ==
  '#/components/schemas/TerminalInput'
raise 'terminal input receipt schema changed' unless
  post.dig('responses', '202', 'content', 'application/json', 'schema', '$ref') ==
  '#/components/schemas/TerminalInputReceipt'

puts "API_SCHEMA_PASS paths=#{paths.length} observation=/v1/diagnostics/observation terminal_input=/v1/terminal-sessions/{session_id}/input"

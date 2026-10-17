#!/usr/bin/env ruby
# Read-only physical association acceptance; never restarts, launches, joins,
# moves, focuses or injects input. Evidence contains API state, not credentials.
require 'json'
require 'net/http'
require 'fileutils'
BASE=URI(ENV.fetch('BLIT_LIVE','http://192.168.10.54:8080'))
TOKEN=ENV.fetch('BLIT_AUTH_TOKEN')
EXPECTED=ENV.fetch('BLIT_EXPECT_STATE','ready')
UID_EXPECTED=Integer(ENV.fetch('BLIT_EXPECT_UID','1000'))
EVIDENCE=ENV.fetch('BLIT_EVIDENCE_DIR','diagnostics/association-539/physical')
raise 'expected state must be ready or waiting' unless %w[ready waiting].include?(EXPECTED)
FileUtils.mkdir_p(EVIDENCE)
def observation
  q=Net::HTTP::Get.new(BASE+'/v1/diagnostics/observation')
  q['Authorization']="Bearer #{TOKEN}"
  r=Net::HTTP.start(BASE.host,BASE.port,open_timeout:2,read_timeout:10){|h|h.request(q)}
  raise "observation HTTP #{r.code}" unless r.code=='200'
  JSON.parse(r.body)
end
def check(ok,message) = (raise(message) unless ok)
previous=nil
3.times do |index|
  current=observation
  File.write(File.join(EVIDENCE,"sample-#{index}.json"),JSON.pretty_generate(current))
  login=current.fetch('login');identity=login.fetch('identity')
  check(login['desktop'].is_a?(Hash),'server lacks native association API; no mutations performed')
  desktop=login.fetch('desktop')
  session=login.fetch('session');check(session && session['login_manager'],'no supervised login')
  check(current.dig('sessions','sessions').count{|s|s['login_manager']}==1,'duplicate login boundaries')
  if EXPECTED=='waiting'
    check(identity['state']=='waiting' && desktop['state']=='inactive','pre-authentication association trusted')
    check(desktop['owner'].nil? && desktop['client'].nil?,'pre-authentication owner/client exposed')
  else
    check(identity['state']=='verified' && identity['uid']==UID_EXPECTED &&
      identity['session_kind']=='desktop','wrong authenticated identity or kind')
    check(desktop['state']=='ready',"native association not ready:#{desktop['reason']}")
    owner=desktop.fetch('owner');client=desktop.fetch('client')
    check(owner['uid']==UID_EXPECTED && owner['pid']>1 && owner['starttime_ticks']>0,'unverified owner lifetime')
    check(client['name']==desktop['client_name'],'wrong native client')
    pane=current.dig('topology','panes').find{|p|p['id']==session['pane_id']}
    check(pane && client['rows']==pane.dig('cell_grid','rows') &&
      client['columns']==pane.dig('cell_grid','columns'),'native client grid differs from physical pane')
    age=current.fetch('captured_monotonic_us')-desktop['observed_monotonic_us']
    check(age>=0 && age<=3_000_000,'stale association advertised ready')
    if previous
      old=previous.fetch('login')
      check(session['id']==old.dig('session','id') && owner==old.dig('desktop','owner'),
        'login or owner changed during bounded check; repeat on settled session')
      check(desktop['observed_monotonic_us']>old.dig('desktop','observed_monotonic_us'),
        'native observation did not refresh without probe requests')
      check(desktop['observed_monotonic_us']>previous['captured_monotonic_us']+250_000,
        'native refresh happened only immediately after the preceding probe')
    end
  end
  previous=current
  # Exceed the freshness TTL with no intervening probe requests. The next
  # response must contain a genuinely newer background observation.
  sleep 3.2 unless index==2
end
desktop=previous.dig('login','desktop')
puts "HOST_NATIVE_ASSOCIATION_PASS state=#{EXPECTED} uid=#{UID_EXPECTED} owner_pid=#{desktop.dig('owner','pid') || 'none'} pane=#{previous.dig('login','pane_id')} samples=3 mutations=0"

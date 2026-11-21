const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const workflow=JSON.parse(fs.readFileSync(path.resolve(__dirname,'..','integrations/n8n/sentinel-rf.workflow.json'),'utf8'));
let state={};
function execute(name,json={},items=[]){const node=workflow.nodes.find(n=>n.name===name);return new Function('$json','$getWorkflowStaticData','$input',node.parameters.jsCode)(json,()=>state,{all:()=>items})}
const event={event:'SIGNAL_WEAK',boot_id:'0123456789abcdef',sequence:1,timestamp_ms:100,simulated:false,notify:true,rssi_dbm:-82,distance_estimated_m:null,client_id:1};
assert.equal(execute('Validate and deduplicate',{body:{...event,boot_id:'invalid'}}).length,0);
assert.equal(execute('Validate and deduplicate',{body:{...event,simulated:true}}).length,0);
assert.equal(execute('Validate and deduplicate',{body:{...event,rssi_dbm:0}}).length,0);
let result=execute('Validate and deduplicate',{body:event});assert.equal(result.length,1);
assert(result[0].json.message.includes('sin calibración'));
execute('Commit delivery',{},result);
assert.equal(execute('Validate and deduplicate',{body:event}).length,0);
assert.equal(execute('Check heartbeat').length,0);
state.lastSeen=Date.now()-61000;
result=execute('Check heartbeat');assert.equal(result.length,1);
execute('Commit delivery',{},result);assert.equal(execute('Check heartbeat').length,0);
execute('Validate and deduplicate',{body:{...event,event:'HEARTBEAT',sequence:2}});
assert.equal(state.offline,false);assert.equal(execute('Check heartbeat').length,0);
console.log('test_workflow: ALL TESTS PASSED (no n8n runtime/network)');

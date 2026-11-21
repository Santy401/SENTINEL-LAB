import { NodeOperationError } from 'n8n-workflow';
import type { IExecuteFunctions, INodeExecutionData, INodeType, INodeTypeDescription } from 'n8n-workflow';
export class SentinelTelegram implements INodeType {
  description: INodeTypeDescription = {
    displayName:'SENTINEL Telegram',name:'sentinelTelegram',group:['output'],version:1,
    description:'Send a lab alert with token and destination stored in credentials',
    defaults:{name:'SENTINEL Telegram'},inputs:['main'],outputs:['main'],
    credentials:[{name:'sentinelTelegram',required:true}],properties:[],
  };
  async execute(this: IExecuteFunctions): Promise<INodeExecutionData[][]> {
    const credentials = await this.getCredentials('sentinelTelegram');
    const items = this.getInputData();
    for (const item of items) {
      const message = item.json.message;
      if (typeof message !== 'string' || message.length > 4000)
        throw new NodeOperationError(this.getNode(), 'Invalid message');
      try {
        const response = await this.helpers.httpRequest({
          method:'POST',url:`https://api.telegram.org/bot${credentials.botToken}/sendMessage`,
          body:{chat_id:credentials.chatId,text:message},json:true,timeout:5000,
        });
        if (!response.ok) throw new Error('Delivery rejected');
      } catch {
        // Never propagate a request error containing the token-bearing URL.
        throw new NodeOperationError(this.getNode(), 'Telegram delivery failed');
      }
    }
    return [items];
  }
}

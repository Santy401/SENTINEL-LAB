import type { ICredentialType, INodeProperties } from 'n8n-workflow';
export class SentinelTelegram implements ICredentialType {
  name = 'sentinelTelegram';
  displayName = 'SENTINEL Telegram';
  properties: INodeProperties[] = [
    {displayName:'Bot Token',name:'botToken',type:'string',typeOptions:{password:true},default:'',required:true},
    {displayName:'Chat ID',name:'chatId',type:'string',typeOptions:{password:true},default:'',required:true},
  ];
}

**Why we removed user credentials from license.config?**

for protecting the user credential from hacking the user credentials have been removed from the config file

**How license are validated?**

**"organizationId"** is introdused into the nx server where the user have to register the organization to network optics and add into the server. 

to get this information plugin don't need user creditial for creating session. "https://" + m_host + "/rest/v3/system/info" url will give the json containing the "organizationId" detail.

**Deprected Post Event Functionality**

As the user credentials have been removed from the license.config file the session to media server can not be created. due to this we can not generate event notification. So we have to remove this functionality from the enterprise version

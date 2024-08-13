--[[
    Squint client script
        by Florian Dormont.

    Based on a sample project by Lampysprites.

    This script will keep running for a while if the dialog window 
    is closed with an "x" button :(

    If that happens, close the sprite and open again (Shift + Ctrl + T)
]]

local web_socket
local dialog
local current_sprite

--[[
    Set up an image buffer for two reasons to leverage Aseprite's own layer blending
    and to make sure that the whole sprite is sent to squint.
]]
local image_buffer

-- Send image command identifier
local IMAGE_ID = string.byte("I")


-- Forward declarations
local send_image_to_squint
local on_site_change


-- Clean up and exit
local function finish()
    if web_socket ~= nil then web_socket:close() end
    if dialog ~= nil then dialog:close() end
    if current_sprite ~= nil then current_sprite.events:off(send_image_to_squint) end
    app.events:off(on_site_change)
    current_sprite = nil
    dialog = nil
end

local function set_sprite_hooks(sprite)
    sprite.events:on('change', send_image_to_squint)
end

local function unset_sprite_hooks(sprite)
    sprite.events:off(send_image_to_squint)
end

local function setup_image_buffer()
    if current_sprite == nil then
        return
    end
    
    local width = current_sprite.width*current_sprite.pixelRatio.width
    local height = current_sprite.height*current_sprite.pixelRatio.height
    
    if image_buffer == nil then
        image_buffer = Image(width, height, ColorMode.RGB)
    elseif image_buffer.width ~= width or image_buffer.height ~= height then
        image_buffer:resize(width, height)
    end
end

send_image_to_squint = function()
    setup_image_buffer()

    if image_buffer ~= nil and app.activeFrame ~= nil then
        image_buffer:clear()
        
        if current_sprite.pixelRatio.width == 1 and current_sprite.pixelRatio.height == 1 then
            -- unfortunately the following does not take the sprite pixelRatio into account:
            image_buffer:drawSprite(current_sprite, app.activeFrame.frameNumber)
        else
            -- (warning: slow code below)

            -- so we first flatten the sprite:
            local flatten_image = Image(current_sprite.width, current_sprite.height, current_sprite.colorMode)
            local flatten_frame = current_sprite.frames[app.activeFrame.frameNumber]
            
            for _, layer in ipairs(current_sprite.layers) do
              if layer.isVisible then
                local cel = layer:cel(flatten_frame)
                if cel then
                    local image = cel.image
                    flatten_image:drawImage(image, cel.position)
                end
              end
            end
                
            -- and then we put each pixel one by one:       
            for it in image_buffer:pixels() do
                local x = it.x/current_sprite.pixelRatio.width
                local y = it.y/current_sprite.pixelRatio.height
                local c = flatten_image:getPixel(x, y)
                if flatten_image.colorMode == ColorMode.INDEXED then
                    local color = current_sprite.palettes[1]:getColor(c)
                    c = app.pixelColor.rgba(color.red, color.green, color.blue, 255)
                elseif flatten_image.colorMode == ColorMode.GRAY then
                    c = app.pixelColor.rgba(c, c, c, 255)
                end
                it(c)
            end
        end
        
        web_socket:sendBinary(string.pack("<LLL", IMAGE_ID, image_buffer.width, image_buffer.height), image_buffer.bytes)
    end
end


-- close connection and ui if the sprite is closed
local frame = -1
on_site_change = function(send_trigger)
    if app.activeSprite ~= current_sprite then
        if current_sprite ~= nil then
           unset_sprite_hooks(current_sprite)
        end

        if app.activeSprite ~= nil then 
            current_sprite = app.activeSprite
            set_sprite_hooks(current_sprite)
            send_trigger = true
        end

        set_sprite_hooks(current_sprite)
    elseif current_sprite ~= nil then
        -- update the view after the frame changes
        if app.activeFrame.frameNumber ~= frame then
            frame = app.activeFrame.frameNumber
            send_trigger = true
        end
    end
    
    if send_trigger then
        send_image_to_squint()
    end
end


local function on_squint_connection(message_type, message)
    if message_type == WebSocketMessageType.OPEN then
        dialog:modify{id="status", text="Connected"}
        app.events:on('sitechange', on_site_change)
        on_site_change(true)
    elseif message_type == WebSocketMessageType.CLOSE and dialog ~= nil then
        dialog:modify{id="status", text="No connection"}
        app.events:off(on_site_change)
    end
end

function exit(plugin)
    finish()
end

function init(plugin)
    plugin:newCommand {
        id="SquintStartClient",
        title="Connect to Squint",
        group="file_scripts",
        onclick=function()
            dialog = Dialog {
                title="Squint client"
            }
            -- Set up a websocket
            -- TODO once I get an easy way to build squint with zlib, switch deflate to true
            web_socket = WebSocket{ url="http://127.0.0.1:34613", onreceive=on_squint_connection, deflate=false }

            -- Create the connection status popup
            dialog:label{ id="status", text="Connecting..." }
            dialog:button{ text="Close the connection", onclick=finish}

            -- GO
            web_socket:connect()
            dialog:show{ wait=false }
        end
    }
end
